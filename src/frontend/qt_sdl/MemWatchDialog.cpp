/*
    Copyright 2016-2025 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include "MemWatchDialog.h"
#include "Platform.h"
#include "main.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QListWidget>
#include <QScrollBar>
#include <QBrush>
#include <QFont>
#include <QDropEvent>
#include <QMenu>
#include <QMenuBar>
#include <QAction>
#include <QSettings>
#include <QKeyEvent>

using namespace melonDS;

MemWatchDialog *MemWatchDialog::currentDlg = nullptr;

// --- CUSTOM TABLE WIDGET FOR DRAG AND DROP --------------------------------

MemWatchTableWidget::MemWatchTableWidget(MemWatchDialog *parent)
    : QTableWidget(parent), dialog(parent), dragSourceRow(-1)
{
}

void MemWatchTableWidget::startDrag(Qt::DropActions supportedActions)
{
    // Save the source row before the drag starts
    dragSourceRow = currentRow();
    QTableWidget::startDrag(supportedActions);
}

void MemWatchTableWidget::dropEvent(QDropEvent *event)
{
    // Don't process if we don't have a valid source row
    if (dragSourceRow < 0)
    {
        QTableWidget::dropEvent(event);
        return;
    }

    // Calculate the target row from the drop position
    QTableWidgetItem *itemAtDrop = itemAt(event->position().toPoint());
    int toRow = itemAtDrop ? row(itemAtDrop) : rowCount() - 1;

    int fromRow = dragSourceRow;
    dragSourceRow = -1; // Reset

    // Validate indices
    if (fromRow < 0 || toRow < 0 || fromRow >= rowCount() || toRow >= rowCount())
    {
        event->ignore();
        return;
    }

    // If dropping on the same row, do nothing
    if (fromRow == toRow)
    {
        event->ignore();
        return;
    }

    // Accept and ignore the event - we'll handle everything ourselves
    event->setDropAction(Qt::IgnoreAction);
    event->accept();

    // Update the underlying data and rebuild the table
    dialog->handleRowMoved(fromRow, toRow);

    // Select the moved row at its new position
    selectRow(toRow);
}

void MemWatchTableWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
        int row = currentRow();
        if (row >= 0)
        {
            dialog->removeCurrentWatch();
            event->accept();
            return;
        }
    }

    QTableWidget::keyPressEvent(event);
}

// --- MEMWATCH DIALOG -------------------------------------------------------

MemWatchDialog::MemWatchDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName("MemWatchDialog");
    setWindowTitle("Memory Watches - melonDS");
    setAttribute(Qt::WA_DeleteOnClose);
    resize(700, 400);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Create menubar
    QMenuBar *menuBar = new QMenuBar(this);

    // Watch menu
    QMenu *watchMenu = menuBar->addMenu("&Watch");

    QAction *addAction = watchMenu->addAction("&Add Watch");
    connect(addAction, &QAction::triggered, this, &MemWatchDialog::onAddWatch);

    QAction *editAction = watchMenu->addAction("&Edit Watch");
    connect(editAction, &QAction::triggered, this, &MemWatchDialog::onEditWatch);

    QAction *removeAction = watchMenu->addAction("&Remove Watch");
    connect(removeAction, &QAction::triggered, this, &MemWatchDialog::onRemoveWatch);

    watchMenu->addSeparator();

    QAction *clearAction = watchMenu->addAction("&Clear All");
    connect(clearAction, &QAction::triggered, this, &MemWatchDialog::onClearAll);

    // File menu
    QMenu *fileMenu = menuBar->addMenu("&File");

    QAction *saveAction = fileMenu->addAction("&Save List...");
    connect(saveAction, &QAction::triggered, this, &MemWatchDialog::onSaveWatches);

    QAction *loadAction = fileMenu->addAction("&Load List...");
    connect(loadAction, &QAction::triggered, this, &MemWatchDialog::onLoadWatches);

    fileMenu->addSeparator();

    recentFilesMenu = fileMenu->addMenu("Open &Recent");
    UpdateRecentFilesMenu();

    mainLayout->addWidget(menuBar);

    // Create table
    watchTable = new MemWatchTableWidget(this);
    watchTable->setColumnCount(5);
    watchTable->setHorizontalHeaderLabels({"Label", "Address", "Type", "Format", "Value"});
    watchTable->horizontalHeader()->setStretchLastSection(true);
    watchTable->verticalHeader()->setVisible(false);
    watchTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    watchTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Enable drag and drop for reordering
    watchTable->setDragEnabled(true);
    watchTable->setAcceptDrops(true);
    watchTable->setDragDropMode(QAbstractItemView::InternalMove);
    watchTable->setDropIndicatorShown(true);
    watchTable->setDefaultDropAction(Qt::MoveAction);

    // Enable context menu
    watchTable->setContextMenuPolicy(Qt::CustomContextMenu);

    QWidget *tableContainer = new QWidget(this);
    QVBoxLayout *tableLayout = new QVBoxLayout(tableContainer);
    tableLayout->setContentsMargins(6, 6, 6, 6);
    tableLayout->addWidget(watchTable);

    mainLayout->addWidget(tableContainer);

    // Connect signals
    connect(watchTable, &QTableWidget::cellDoubleClicked, this, &MemWatchDialog::onCellDoubleClicked);
    connect(watchTable, &QWidget::customContextMenuRequested, this, &MemWatchDialog::showContextMenu);

    // Create and start update thread
    updateThread = new MemWatchThread(this);
    connect(updateThread, &MemWatchThread::updateValuesSignal, this, &MemWatchDialog::onUpdateValuesSignal);
    updateThread->Start();
}

MemWatchDialog::~MemWatchDialog()
{
    if (updateThread)
    {
        disconnect(updateThread, nullptr, this, nullptr);
        updateThread->Stop();
        delete updateThread;
        updateThread = nullptr;
    }
}

melonDS::NDS *MemWatchDialog::GetNDS()
{
    EmuInstance *emuInstance = ((MainWindow *)this->parent())->getEmuInstance();
    if (emuInstance)
    {
        return emuInstance->getNDS();
    }
    return nullptr;
}

void *MemWatchDialog::GetRAM(uint32_t address)
{
    melonDS::NDS *nds = this->GetNDS();

    if (nds != nullptr)
    {
        if (address < nds->ARM9.ITCMSize)
        {
            return &nds->ARM9.ITCM[address & (ITCMPhysicalSize - 1)];
        }
        else if (nds->ARM9.DTCM != nullptr && (address & nds->ARM9.DTCMMask) == nds->ARM9.DTCMBase)
        {
            return &nds->ARM9.DTCM[address & (DTCMPhysicalSize - 1)];
        }
        else if ((address & 0xFFFFF000) == 0xFFFF0000)
        {
            return (void *)&nds->GetARM9BIOS()[address & 0xFFF];
        }

        switch (address & 0xFF000000)
        {
        case 0x02000000:
            if (nds->MainRAM != nullptr)
            {
                return &nds->MainRAM[address & nds->MainRAMMask];
            }
            break;
        case 0x03000000:
            if (nds->SWRAM_ARM9.Mem != nullptr)
            {
                return &nds->SWRAM_ARM9.Mem[address & nds->SWRAM_ARM9.Mask];
            }
            break;
        case 0x05000000:
            return &nds->GPU.Palette[address & 0x7FF];
        case 0x07000000:
            return &nds->GPU.OAM[address & 0x7FF];
        default:
            break;
        }
    }

    return nullptr;
}

uint32_t MemWatchDialog::GetEffectiveAddress(const WatchEntry &entry, bool *valid)
{
    *valid = false;
    uint32_t address = entry.address;

    // If it's not a pointer, the base address is the final destination
    if (!entry.isPointer)
    {
        *valid = true;
        return address;
    }

    // Read base pointer value
    void *pBase = GetRAM(address);
    if (!pBase)
        return 0;

    uint32_t ptrValue = *(uint32_t *)pBase;

    // Follow the pointer chain
    for (int i = 0; i < entry.pointerOffsets.size(); i++)
    {
        int32_t offset = entry.pointerOffsets[i];
        bool isLastOffset = (i == entry.pointerOffsets.size() - 1);

        // Apply the offset to the value we read
        address = ptrValue + offset;

        // If it's the last offset and it's NOT a pointer, we found our final address
        if (isLastOffset && !entry.isLastLevelPointer)
        {
            *valid = true;
            return address;
        }

        // Otherwise, dereference the current address to get the next pointer value
        void *pNext = GetRAM(address);
        if (!pNext)
            return 0;
        ptrValue = *(uint32_t *)pNext;
    }

    // If the chain ends as a pointer (or had no offsets but was marked as a pointer),
    // the final destination address is the value we just dereferenced.
    if (entry.isLastLevelPointer)
    {
        address = ptrValue;
    }

    *valid = true;
    return address;
}

QString MemWatchDialog::ReadStringValue(const WatchEntry &entry, bool *valid)
{
    *valid = false;

    bool addrValid = false;
    uint32_t address = GetEffectiveAddress(entry, &addrValid);
    if (!addrValid)
        return "";

    QString result;
    const int MAX_STRING_LENGTH = 128; // Cap length to prevent massive UI lag

    for (int i = 0; i < MAX_STRING_LENGTH; i++)
    {
        void *pRAM = GetRAM(address + i);
        if (!pRAM)
            break; // Reached invalid memory boundary

        char c = *(char *)pRAM;

        if (c == '\0')
        {
            *valid = true;
            break;
        }

        // Only append printable ASCII characters to avoid UI glitching
        if (c >= 32 && c <= 126)
        {
            result.append(QChar(c));
        }
        else
        {
            result.append('.'); // Replace unprintable chars
        }
    }

    *valid = true;
    return result;
}

QString MemWatchDialog::ReadFourCCValue(const WatchEntry &entry, bool *valid)
{
    *valid = false;

    bool addrValid = false;
    uint32_t address = GetEffectiveAddress(entry, &addrValid);
    if (!addrValid)
        return "";

    void *pRAM = GetRAM(address);
    if (!pRAM)
        return "";
    // if memory read 0MMR, RRM0 should be returned
    char chars[5];
    for (int i = 0; i < 4; i++)
    {
        chars[i] = *(char *)((uint8_t *)pRAM + (3 - i));
    }
    chars[4] = '\0';

    *valid = true;
    return QString(chars);
}

uint32_t MemWatchDialog::ReadValue(const WatchEntry &entry, bool *valid)
{
    *valid = false;

    bool addrValid = false;
    uint32_t address = GetEffectiveAddress(entry, &addrValid);
    if (!addrValid)
        return 0;

    void *pRAM = GetRAM(address);
    if (!pRAM)
        return 0;

    *valid = true;

    switch (entry.dataType)
    {
    case watchDataType_Byte:
        return *(uint8_t *)pRAM;
    case watchDataType_Halfword:
        return *(uint16_t *)pRAM;
    case watchDataType_Word:
        return *(uint32_t *)pRAM;
    }

    return 0;
}

void MemWatchDialog::WriteValue(WatchEntry &entry, uint32_t value)
{
    bool addrValid = false;
    uint32_t address = GetEffectiveAddress(entry, &addrValid);
    if (!addrValid)
        return;

    void *pRAM = GetRAM(address);
    if (!pRAM)
        return;

    switch (entry.dataType)
    {
    case watchDataType_Byte:
        *(uint8_t *)pRAM = (uint8_t)value;
        break;
    case watchDataType_Halfword:
        *(uint16_t *)pRAM = (uint16_t)value;
        break;
    case watchDataType_Word:
        *(uint32_t *)pRAM = value;
        break;
    }
}

QString MemWatchDialog::FormatValue(uint32_t value, WatchDisplayFormat format, WatchDataType dataType)
{
    switch (format)
    {
    case watchDisplayFormat_Hex:
    {
        int width = (dataType == watchDataType_Byte) ? 2 : (dataType == watchDataType_Halfword) ? 4
                                                                                                : 8;
        return QString("0x%1").arg(value, width, 16, QChar('0')).toUpper().replace("0X", "0x");
    }
    case watchDisplayFormat_Decimal:
    case watchDisplayFormat_Unsigned:
        return QString::number(value);
    case watchDisplayFormat_q20Float:
        return QString::number(q20ToFloat((int32_t)value), 'f', 6);
    case watchDisplayFormat_Signed:
    {
        int32_t signedValue;
        switch (dataType)
        {
        case watchDataType_Byte:
            signedValue = (int8_t)value;
            break;
        case watchDataType_Halfword:
            signedValue = (int16_t)value;
            break;
        case watchDataType_Word:
            signedValue = (int32_t)value;
            break;
        }
        return QString::number(signedValue);
    }
    }
    return "???";
}

void MemWatchDialog::RebuildTable()
{
    QMutexLocker locker(&watchMutex);

    // Save current scroll position
    int scrollPos = watchTable->verticalScrollBar()->value();

    // Clear the table
    watchTable->setRowCount(0);

    // Add all watches
    for (const WatchEntry &entry : watches)
    {
        AddWatchToTable(entry);
    }

    // Restore scroll position
    watchTable->verticalScrollBar()->setValue(scrollPos);

    // Update all values (mutex is already locked, so update directly)
    for (int i = 0; i < watches.size(); i++)
    {
        UpdateTableRow(i);
    }
}

void MemWatchDialog::AddWatchToTable(const WatchEntry &entry)
{
    int row = watchTable->rowCount();
    watchTable->insertRow(row);
    watchTable->setRowHeight(row, watchTable->rowHeight(row) / 2);

    // Handle separators specially
    if (entry.isSeparator)
    {
        QTableWidgetItem *separatorItem = new QTableWidgetItem("");
        separatorItem->setBackground(QBrush(QColor(220, 220, 220)));
        separatorItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

        watchTable->setItem(row, 0, separatorItem);
        watchTable->setSpan(row, 0, 1, 5);
        watchTable->setRowHeight(row, watchTable->rowHeight(row) / 2);
        return;
    }

    watchTable->setItem(row, 0, new QTableWidgetItem(entry.label));

    QString addrStr = QString("0x%1").arg(entry.address, 8, 16, QChar('0')).toUpper().replace("0X", "0x");
    if (entry.isPointer)
    {
        addrStr = "[" + addrStr + "]";
        for (int32_t offset : entry.pointerOffsets)
        {
            if (offset >= 0)
                addrStr += QString("+0x%1").arg(offset, 1, 16).toUpper().replace("0X", "0x");
            else
                addrStr += QString("-0x%1").arg(-offset, 1, 16).toUpper().replace("0X", "0x");
        }
    }
    watchTable->setItem(row, 1, new QTableWidgetItem(addrStr));

    QString typeStr = GetDataTypeString(entry.dataType);
    watchTable->setItem(row, 2, new QTableWidgetItem(typeStr));

    QString formatStr = GetDisplayFormatString(entry.displayFormat);
    watchTable->setItem(row, 3, new QTableWidgetItem(formatStr));

    watchTable->setItem(row, 4, new QTableWidgetItem("---"));
}

void MemWatchDialog::UpdateTableRow(int row)
{
    if (row < 0 || row >= watches.size())
        return;

    const WatchEntry &entry = watches[row];

    // Skip separators
    if (entry.isSeparator)
        return;

    bool valid = false;
    QString valueStr;


    if (entry.dataType == watchDataType_String)
    {
        valueStr = ReadStringValue(entry, &valid);
        if (!valid)
            valueStr = "INVALID";
    }
    else if (entry.dataType == watchDataType_FourCC)
    {
        valueStr = ReadFourCCValue(entry, &valid);
        if (!valid)
            valueStr = "INVALID";
    }
    else
    {
        uint32_t value = ReadValue(entry, &valid);
        if (valid)
            valueStr = FormatValue(value, entry.displayFormat, entry.dataType);
        else
            valueStr = "INVALID";
    }

    watchTable->item(row, 4)->setText(valueStr);
    }

void MemWatchDialog::UpdateWatchValues()
{
    QMutexLocker locker(&watchMutex);
    for (int i = 0; i < watches.size(); i++)
    {
        UpdateTableRow(i);
    }
}

void MemWatchDialog::done(int r)
{
    QDialog::done(r);
    closeDlg();
}

void MemWatchDialog::onAddWatch()
{
    QDialog *addDialog = new QDialog(this);
    addDialog->setWindowTitle("Add Watch");
    addDialog->resize(400, 350);

    QVBoxLayout *layout = new QVBoxLayout(addDialog);
    QFormLayout *formLayout = new QFormLayout();

    QLineEdit *labelEdit = new QLineEdit();
    QLineEdit *addressEdit = new QLineEdit();
    addressEdit->setPlaceholderText("0x02000000");

    QComboBox *typeCombo = new QComboBox();
    typeCombo->addItem("Byte (8-bit)");
    typeCombo->addItem("Halfword (16-bit)");
    typeCombo->addItem("Word (32-bit)");
    typeCombo->addItem("FourCC (4-char ID)");
    typeCombo->addItem("String (ASCII)");


    QComboBox *formatCombo = new QComboBox();
    formatCombo->addItem("Hexadecimal");
    formatCombo->addItem("Decimal");
    formatCombo->addItem("Signed");
    formatCombo->addItem("Unsigned");

    QCheckBox *pointerCheck = new QCheckBox("This is a pointer");

    QListWidget *offsetList = new QListWidget();
    offsetList->setMaximumHeight(100);
    offsetList->setEnabled(false);

    QCheckBox *lastLevelPointerCheck = new QCheckBox("Last level is a pointer");
    lastLevelPointerCheck->setEnabled(false);

    QPushButton *addOffsetBtn = new QPushButton("Add Offset");
    QPushButton *removeOffsetBtn = new QPushButton("Remove Offset");
    addOffsetBtn->setEnabled(false);
    removeOffsetBtn->setEnabled(false);

    connect(pointerCheck, &QCheckBox::toggled, [=](bool checked)
            {
        offsetList->setEnabled(checked);
        addOffsetBtn->setEnabled(checked);
        removeOffsetBtn->setEnabled(checked);
        lastLevelPointerCheck->setEnabled(checked); });

    connect(addOffsetBtn, &QPushButton::clicked, [=]()
            {
        bool ok;
        QString text = QInputDialog::getText(addDialog, "Add Offset", 
                                            "Enter offset (hex with 0x or decimal):", 
                                            QLineEdit::Normal, "0x0", &ok);
        if (ok && !text.isEmpty())
        {
            offsetList->addItem(text);
        } });

    connect(removeOffsetBtn, &QPushButton::clicked, [=]()
            {
        QListWidgetItem* item = offsetList->currentItem();
        if (item)
        {
            delete offsetList->takeItem(offsetList->row(item));
        } });

    formLayout->addRow("Label:", labelEdit);
    formLayout->addRow("Address:", addressEdit);
    formLayout->addRow("Data Type:", typeCombo);
    formLayout->addRow("Display Format:", formatCombo);
    formLayout->addRow(pointerCheck);
    formLayout->addRow("Pointer Offsets:", offsetList);

    QHBoxLayout *offsetBtnLayout = new QHBoxLayout();
    offsetBtnLayout->addWidget(addOffsetBtn);
    offsetBtnLayout->addWidget(removeOffsetBtn);
    formLayout->addRow(offsetBtnLayout);

    layout->addLayout(formLayout);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, addDialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, addDialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    if (addDialog->exec() == QDialog::Accepted)
    {
        WatchEntry entry;
        entry.label = labelEdit->text();

        QString addrStr = addressEdit->text();
        if (addrStr.startsWith("0x", Qt::CaseInsensitive))
            entry.address = addrStr.mid(2).toUInt(nullptr, 16);
        else
            entry.address = addrStr.toUInt();

        entry.dataType = (WatchDataType)typeCombo->currentIndex();
        entry.displayFormat = (WatchDisplayFormat)formatCombo->currentIndex();
        entry.isPointer = pointerCheck->isChecked();
        entry.isLastLevelPointer = lastLevelPointerCheck->isChecked();

        if (entry.isPointer)
        {
            for (int i = 0; i < offsetList->count(); i++)
            {
                QString offsetStr = offsetList->item(i)->text();
                int32_t offset;
                if (offsetStr.startsWith("0x", Qt::CaseInsensitive))
                    offset = offsetStr.mid(2).toInt(nullptr, 16);
                else
                    offset = offsetStr.toInt();
                entry.pointerOffsets.append(offset);
            }
        }

        QMutexLocker locker(&watchMutex);
        watches.append(entry);
        AddWatchToTable(entry);
    }

    delete addDialog;
}

void MemWatchDialog::onRemoveWatch()
{
    int row = watchTable->currentRow();
    if (row >= 0 && row < watches.size())
    {
        QMutexLocker locker(&watchMutex);
        watches.removeAt(row);
        locker.unlock();

        RebuildTable();
    }
}

void MemWatchDialog::removeCurrentWatch()
{
    onRemoveWatch();
}

void MemWatchDialog::addWatchEntry(const WatchEntry &entry)
{
    QMutexLocker locker(&watchMutex);
    watches.append(entry);
    int newRow = watches.size() - 1;
    locker.unlock();

    RebuildTable();
    watchTable->selectRow(newRow);
}

void MemWatchDialog::onEditWatch()
{
    int row = watchTable->currentRow();
    if (row < 0 || row >= watches.size())
        return;

    WatchEntry &entry = watches[row];

    // Can't edit separators
    if (entry.isSeparator)
        return;

    QDialog *editDialog = new QDialog(this);
    editDialog->setWindowTitle("Edit Watch");
    editDialog->resize(400, 350);

    QVBoxLayout *layout = new QVBoxLayout(editDialog);
    QFormLayout *formLayout = new QFormLayout();

    QLineEdit *labelEdit = new QLineEdit(entry.label);
    QLineEdit *addressEdit = new QLineEdit(QString("0x%1").arg(entry.address, 8, 16, QChar('0')));

    QComboBox *typeCombo = new QComboBox();
    typeCombo->addItem("Byte (8-bit)");
    typeCombo->addItem("Halfword (16-bit)");
    typeCombo->addItem("Word (32-bit)");
    typeCombo->addItem("FourCC (4-char ID)");
    typeCombo->addItem("String (ASCII)");
    typeCombo->setCurrentIndex((int)entry.dataType);

    QComboBox *formatCombo = new QComboBox();
    formatCombo->addItem("Hexadecimal");
    formatCombo->addItem("Decimal");
    formatCombo->addItem("Signed");
    formatCombo->addItem("Unsigned");
    formatCombo->setCurrentIndex((int)entry.displayFormat);

    QCheckBox *pointerCheck = new QCheckBox("This is a pointer");
    pointerCheck->setChecked(entry.isPointer);

    QListWidget *offsetList = new QListWidget();
    offsetList->setMaximumHeight(100);
    offsetList->setEnabled(entry.isPointer);

    for (int32_t offset : entry.pointerOffsets)
    {
        offsetList->addItem(QString("0x%1").arg(offset, 1, 16));
    }

    QCheckBox *lastLevelPointerCheck = new QCheckBox("Last level is a pointer");
    lastLevelPointerCheck->setChecked(entry.isLastLevelPointer);
    lastLevelPointerCheck->setEnabled(entry.isPointer);

    QPushButton *addOffsetBtn = new QPushButton("Add Offset");
    QPushButton *removeOffsetBtn = new QPushButton("Remove Offset");
    addOffsetBtn->setEnabled(entry.isPointer);
    removeOffsetBtn->setEnabled(entry.isPointer);

    connect(pointerCheck, &QCheckBox::toggled, [=](bool checked)
            {
        offsetList->setEnabled(checked);
        addOffsetBtn->setEnabled(checked);
        removeOffsetBtn->setEnabled(checked);
        lastLevelPointerCheck->setEnabled(checked); });

    connect(addOffsetBtn, &QPushButton::clicked, [=]()
            {
        bool ok;
        QString text = QInputDialog::getText(editDialog, "Add Offset", 
                                            "Enter offset (hex with 0x or decimal):", 
                                            QLineEdit::Normal, "0x0", &ok);
        if (ok && !text.isEmpty())
        {
            offsetList->addItem(text);
        } });

    connect(removeOffsetBtn, &QPushButton::clicked, [=]()
            {
        QListWidgetItem* item = offsetList->currentItem();
        if (item)
        {
            delete offsetList->takeItem(offsetList->row(item));
        } });

    formLayout->addRow("Label:", labelEdit);
    formLayout->addRow("Address:", addressEdit);
    formLayout->addRow("Data Type:", typeCombo);
    formLayout->addRow("Display Format:", formatCombo);
    formLayout->addRow(pointerCheck);
    formLayout->addRow("Pointer Offsets:", offsetList);

    QHBoxLayout *offsetBtnLayout = new QHBoxLayout();
    offsetBtnLayout->addWidget(addOffsetBtn);
    offsetBtnLayout->addWidget(removeOffsetBtn);
    formLayout->addRow(offsetBtnLayout);
    formLayout->addRow(lastLevelPointerCheck);

    layout->addLayout(formLayout);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, editDialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, editDialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    if (editDialog->exec() == QDialog::Accepted)
    {
        QMutexLocker locker(&watchMutex);

        entry.label = labelEdit->text();

        QString addrStr = addressEdit->text();
        if (addrStr.startsWith("0x", Qt::CaseInsensitive))
            entry.address = addrStr.mid(2).toUInt(nullptr, 16);
        else
            entry.address = addrStr.toUInt();

        entry.dataType = (WatchDataType)typeCombo->currentIndex();
        entry.displayFormat = (WatchDisplayFormat)formatCombo->currentIndex();
        entry.isPointer = pointerCheck->isChecked();
        entry.isLastLevelPointer = lastLevelPointerCheck->isChecked();

        entry.pointerOffsets.clear();
        if (entry.isPointer)
        {
            for (int i = 0; i < offsetList->count(); i++)
            {
                QString offsetStr = offsetList->item(i)->text();
                int32_t offset;
                if (offsetStr.startsWith("0x", Qt::CaseInsensitive))
                    offset = offsetStr.mid(2).toInt(nullptr, 16);
                else
                    offset = offsetStr.toInt();
                entry.pointerOffsets.append(offset);
            }
        }

        // Update table display
        watchTable->item(row, 0)->setText(entry.label);

        QString addrStr2 = QString("0x%1").arg(entry.address, 8, 16, QChar('0')).toUpper().replace("0X", "0x");
        if (entry.isPointer)
        {
            addrStr2 = "[" + addrStr2 + "]";
            for (int32_t offset : entry.pointerOffsets)
            {
                if (offset >= 0)
                    addrStr2 += QString("+0x%1").arg(offset, 1, 16).toUpper().replace("0X", "0x");
                else
                    addrStr2 += QString("-0x%1").arg(-offset, 1, 16).toUpper().replace("0X", "0x");
            }
        }
        watchTable->item(row, 1)->setText(addrStr2);

        QString typeStr = GetDataTypeString(entry.dataType);
        watchTable->item(row, 2)->setText(typeStr);

        QString formatStr = GetDisplayFormatString(entry.displayFormat);
        watchTable->item(row, 3)->setText(formatStr);
    }

    delete editDialog;
}

void MemWatchDialog::onCellDoubleClicked(int row, int column)
{
    if (row < 0 || row >= watches.size())
        return;

    // Column 4 is the value column - allow quick value editing
    if (column == 4)
    {
        // Read-Only for strings and FourCC
        if(watches[row].dataType == watchDataType_String || watches[row].dataType == watchDataType_FourCC) 
        {
            return;
        }

        bool ok;
        QString current = watchTable->item(row, column)->text();
        QString input = QInputDialog::getText(this, "Edit Value", "Enter new value:", QLineEdit::Normal, current, &ok);

        if (ok && !input.isEmpty())
        {
            bool convOk;
            uint32_t value;

            if (input.startsWith("0x", Qt::CaseInsensitive))
                value = input.mid(2).toUInt(&convOk, 16);
            else
                value = input.toUInt(&convOk, 10);

            if (convOk)
            {
                QMutexLocker locker(&watchMutex);
                WriteValue(watches[row], value);
            }
        }
    }
    else
    {
        // Any other column - open the full edit dialog
        watchTable->selectRow(row);
        onEditWatch();
    }
}

void MemWatchDialog::onUpdateValuesSignal()
{
    UpdateWatchValues();
}

void MemWatchDialog::onSaveWatches()
{
    QString path = QFileDialog::getSaveFileName(this, "Save Watch List", "", "JSON Files (*.json)");
    if (path.isEmpty())
        return;

    if (!path.endsWith(".json", Qt::CaseInsensitive))
        path += ".json";

    QJsonArray watchArray;
    for (const WatchEntry &entry : watches)
    {
        QJsonObject watchObj;
        watchObj["label"] = entry.label;
        watchObj["address"] = QString("0x%1").arg(entry.address, 8, 16, QChar('0'));
        watchObj["dataType"] = (int)entry.dataType;
        watchObj["displayFormat"] = (int)entry.displayFormat;
        watchObj["isPointer"] = entry.isPointer;
        watchObj["isSeparator"] = entry.isSeparator;
        watchObj["isLastLevelPointer"] = entry.isLastLevelPointer;

        if (entry.isPointer)
        {
            QJsonArray offsetArray;
            for (int32_t offset : entry.pointerOffsets)
                offsetArray.append(offset);
            watchObj["offsets"] = offsetArray;
        }

        watchArray.append(watchObj);
    }

    QJsonDocument doc(watchArray);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(doc.toJson());
        file.close();
    }
}

void MemWatchDialog::onLoadWatches()
{
    QString path = QFileDialog::getOpenFileName(this, "Load Watch List", "", "JSON Files (*.json)");
    if (path.isEmpty())
        return;

    LoadWatchFile(path);
    AddRecentFile(path);
}

void MemWatchDialog::LoadWatchFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);

    QJsonArray watchArray;

    // Handle both array format and object format (with "watches" key)
    if (doc.isArray())
    {
        watchArray = doc.array();
    }
    else if (doc.isObject())
    {
        QJsonObject root = doc.object();
        if (root.contains("watches"))
        {
            watchArray = root["watches"].toArray();
        }
        else
        {
            return;
        }
    }
    else
    {
        return;
    }

    QMutexLocker locker(&watchMutex);

    // Clear watches
    watches.clear();

    // Load watches from JSON
    for (const QJsonValue &val : watchArray)
    {
        QJsonObject watchObj = val.toObject();

        WatchEntry entry;
        entry.label = watchObj["label"].toString();
        entry.isSeparator = watchObj["isSeparator"].toBool();

        // Only load address and other fields if not a separator
        if (!entry.isSeparator)
        {
            QString addrStr = watchObj["address"].toString();
            if (addrStr.startsWith("0x", Qt::CaseInsensitive))
                entry.address = addrStr.mid(2).toUInt(nullptr, 16);
            else
                entry.address = addrStr.toUInt();

            entry.dataType = (WatchDataType)watchObj["dataType"].toInt();
            entry.displayFormat = (WatchDisplayFormat)watchObj["displayFormat"].toInt();
            entry.isPointer = watchObj["isPointer"].toBool();
            entry.isLastLevelPointer = watchObj["isLastLevelPointer"].toBool();

            if (entry.isPointer && watchObj.contains("offsets"))
            {
                QJsonArray offsetArray = watchObj["offsets"].toArray();
                for (const QJsonValue &offsetVal : offsetArray)
                    entry.pointerOffsets.append(offsetVal.toInt());
            }
        }

        watches.append(entry);
    }

    locker.unlock();

    // Rebuild the table (this will lock the mutex itself)
    RebuildTable();
}

void MemWatchDialog::AddRecentFile(const QString &path)
{
    QSettings settings;
    QStringList recentFiles = settings.value("MemWatch/RecentFiles").toStringList();

    // Remove if already exists
    recentFiles.removeAll(path);

    // Add to front
    recentFiles.prepend(path);

    // Keep only last 10
    while (recentFiles.size() > 10)
        recentFiles.removeLast();

    settings.setValue("MemWatch/RecentFiles", recentFiles);

    UpdateRecentFilesMenu();
}

void MemWatchDialog::UpdateRecentFilesMenu()
{
    recentFilesMenu->clear();

    QSettings settings;
    QStringList recentFiles = settings.value("MemWatch/RecentFiles").toStringList();

    if (recentFiles.isEmpty())
    {
        QAction *emptyAction = recentFilesMenu->addAction("(No recent files)");
        emptyAction->setEnabled(false);
        return;
    }

    for (const QString &filePath : recentFiles)
    {
        // Show only the filename in the menu
        QFileInfo fileInfo(filePath);
        QAction *action = recentFilesMenu->addAction(fileInfo.fileName());
        action->setData(filePath);
        action->setToolTip(filePath);
        connect(action, &QAction::triggered, this, &MemWatchDialog::onLoadRecentFile);
    }

    recentFilesMenu->addSeparator();

    QAction *clearAction = recentFilesMenu->addAction("Clear Recent Files");
    connect(clearAction, &QAction::triggered, [this]()
            {
        QSettings settings;
        settings.remove("MemWatch/RecentFiles");
        UpdateRecentFilesMenu(); });
}

void MemWatchDialog::onLoadRecentFile()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;

    QString path = action->data().toString();
    if (path.isEmpty())
        return;

    // Check if file exists
    if (!QFile::exists(path))
    {
        QMessageBox::warning(this, "File Not Found",
                             QString("The file '%1' no longer exists.").arg(path));

        // Remove from recent files
        QSettings settings;
        QStringList recentFiles = settings.value("MemWatch/RecentFiles").toStringList();
        recentFiles.removeAll(path);
        settings.setValue("MemWatch/RecentFiles", recentFiles);
        UpdateRecentFilesMenu();
        return;
    }

    LoadWatchFile(path);
}

void MemWatchDialog::onClearAll()
{
    QMutexLocker locker(&watchMutex);
    watches.clear();
    watchTable->setRowCount(0);
}

void MemWatchDialog::showContextMenu(const QPoint &pos)
{
    QMenu contextMenu(this);

    int currentRow = watchTable->currentRow();

    QAction *addAction = contextMenu.addAction("Add Watch");
    connect(addAction, &QAction::triggered, this, &MemWatchDialog::onAddWatch);

    // Only show remove/edit if a row is selected
    if (currentRow >= 0)
    {
        // Check if it's a separator
        bool isSeparator = (currentRow < watches.size() && watches[currentRow].isSeparator);

        if (!isSeparator)
        {
            QAction *editAction = contextMenu.addAction("Edit Watch");
            connect(editAction, &QAction::triggered, this, &MemWatchDialog::onEditWatch);
        }

        QAction *removeAction = contextMenu.addAction(isSeparator ? "Remove Separator" : "Remove Watch");
        connect(removeAction, &QAction::triggered, this, &MemWatchDialog::onRemoveWatch);

        contextMenu.addSeparator();

        QAction *separatorAbove = contextMenu.addAction("Add Separator Above");
        connect(separatorAbove, &QAction::triggered, this, &MemWatchDialog::onAddSeparatorAbove);

        QAction *separatorBelow = contextMenu.addAction("Add Separator Below");
        connect(separatorBelow, &QAction::triggered, this, &MemWatchDialog::onAddSeparatorBelow);

        contextMenu.addSeparator();
    }

    QAction *saveAction = contextMenu.addAction("Save List...");
    connect(saveAction, &QAction::triggered, this, &MemWatchDialog::onSaveWatches);

    QAction *loadAction = contextMenu.addAction("Load List...");
    connect(loadAction, &QAction::triggered, this, &MemWatchDialog::onLoadWatches);

    contextMenu.addSeparator();

    QAction *clearAction = contextMenu.addAction("Clear All");
    connect(clearAction, &QAction::triggered, this, &MemWatchDialog::onClearAll);

    contextMenu.exec(watchTable->mapToGlobal(pos));
}

void MemWatchDialog::onAddSeparatorAbove()
{
    int row = watchTable->currentRow();
    if (row < 0)
        return;

    QMutexLocker locker(&watchMutex);

    WatchEntry separator;
    separator.isSeparator = true;
    watches.insert(row, separator);

    locker.unlock();

    RebuildTable();
    watchTable->selectRow(row);
}

void MemWatchDialog::onAddSeparatorBelow()
{
    int row = watchTable->currentRow();
    if (row < 0)
        return;

    QMutexLocker locker(&watchMutex);

    WatchEntry separator;
    separator.isSeparator = true;
    watches.insert(row + 1, separator);

    locker.unlock();

    RebuildTable();
    watchTable->selectRow(row + 1);
}

void MemWatchDialog::handleRowMoved(int fromRow, int toRow)
{
    if (fromRow < 0 || fromRow >= watches.size() || toRow < 0 || toRow >= watches.size())
        return;

    // Pause the update thread to prevent interference
    if (updateThread)
        updateThread->Pause();

    QMutexLocker locker(&watchMutex);

    // Move the watch entry from fromRow to toRow
    WatchEntry movedEntry = watches.takeAt(fromRow);
    watches.insert(toRow, movedEntry);

    locker.unlock();

    // Rebuild the table to show the new order
    RebuildTable();

    // Resume the update thread
    if (updateThread)
        updateThread->Unpause();
}

// --- MEMWATCH THREAD --------------------------------------------------------

MemWatchThread::~MemWatchThread()
{
    Stop();
    dialog = nullptr;
}

void MemWatchThread::Start()
{
    start();
    running = true;
}

void MemWatchThread::Stop()
{
    running = false;
    quit();
    wait();
}

void MemWatchThread::run()
{
    while (running)
    {
        if (dialog == nullptr)
        {
            return;
        }

        if (paused)
        {
            continue;
        }

        QThread::msleep(50); // Update every 50ms

        emit updateValuesSignal();
    }
}
