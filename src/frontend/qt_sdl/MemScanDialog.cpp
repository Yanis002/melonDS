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

#include "MemScanDialog.h"
#include "Platform.h"
#include "main.h"
#include "MemWatchDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QSignalBlocker>

using namespace melonDS;

MemScanDialog* MemScanDialog::currentDlg = nullptr;

MemScanDialog::MemScanDialog(QWidget* parent) 
    : QDialog(parent), hasInitialScan(false)
{
    setObjectName("MemScanDialog");
    setWindowTitle("Memory Scanner - melonDS");
    setAttribute(Qt::WA_DeleteOnClose);
    resize(600, 500);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Scan options group
    QGroupBox* optionsGroup = new QGroupBox("Scan Options", this);
    QFormLayout* optionsLayout = new QFormLayout(optionsGroup);

    domainCombo = new QComboBox(this);
    domainCombo->addItem("Main RAM (0x02000000)");
    domainCombo->addItem("ARM9 WRAM (0x03000000)");
    domainCombo->addItem("ARM7 WRAM (0x03800000)");
    optionsLayout->addRow("Memory Domain:", domainCombo);

    typeCombo = new QComboBox(this);
    typeCombo->addItem("Byte (8-bit)");
    typeCombo->addItem("Halfword (16-bit)");
    typeCombo->addItem("Word (32-bit)");
    optionsLayout->addRow("Value Type:", typeCombo);

    scanTypeCombo = new QComboBox(this);
    scanTypeCombo->addItem("Exact Value");
    scanTypeCombo->addItem("Unknown Initial Value");
    scanTypeCombo->addItem("Increased Value");
    scanTypeCombo->addItem("Decreased Value");
    scanTypeCombo->addItem("Changed Value");
    scanTypeCombo->addItem("Unchanged Value");
    connect(scanTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &MemScanDialog::onScanTypeChanged);
    optionsLayout->addRow("Scan Type:", scanTypeCombo);

    valueEdit = new QLineEdit(this);
    valueEdit->setPlaceholderText("Enter value (hex: 0x...)");
    optionsLayout->addRow("Value:", valueEdit);

    mainLayout->addWidget(optionsGroup);

    // Scan buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    btnNewScan = new QPushButton("New Scan", this);
    btnNextScan = new QPushButton("Next Scan", this);
    btnNextScan->setEnabled(false);
    buttonLayout->addWidget(btnNewScan);
    buttonLayout->addWidget(btnNextScan);
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);

    // Results table
    resultCountLabel = new QLabel("Results: 0", this);
    mainLayout->addWidget(resultCountLabel);

    resultsTable = new QTableWidget(this);
    resultsTable->setColumnCount(3);
    resultsTable->setHorizontalHeaderLabels({"Address", "Value", "Previous"});
    resultsTable->horizontalHeader()->setStretchLastSection(true);
    resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultsTable->verticalHeader()->setVisible(false);
    mainLayout->addWidget(resultsTable);

    // Add to watch button
    btnAddToWatch = new QPushButton("Add Selected to Watch List", this);
    btnAddToWatch->setEnabled(false);
    mainLayout->addWidget(btnAddToWatch);

    // Connect signals
    connect(btnNewScan, &QPushButton::clicked, this, &MemScanDialog::onNewScan);
    connect(btnNextScan, &QPushButton::clicked, this, &MemScanDialog::onNextScan);
    connect(btnAddToWatch, &QPushButton::clicked, this, &MemScanDialog::onAddToWatchList);
}

MemScanDialog::~MemScanDialog()
{
}

melonDS::NDS* MemScanDialog::GetNDS()
{
    EmuInstance* emuInstance = ((MainWindow*)this->parent())->getEmuInstance();
    if (emuInstance)
    {
        return emuInstance->getNDS();
    }
    return nullptr;
}

void* MemScanDialog::GetRAM(uint32_t address)
{
    melonDS::NDS* nds = GetNDS();
    if (!nds) return nullptr;

    switch (currentDomain)
    {
        case memDomain_MainRAM:
            if (nds->MainRAM && address <= nds->MainRAMMask)
                return &nds->MainRAM[address & nds->MainRAMMask];
            break;
        case memDomain_ARM9:
            if (nds->SWRAM_ARM9.Mem && address <= nds->SWRAM_ARM9.Mask)
                return &nds->SWRAM_ARM9.Mem[address & nds->SWRAM_ARM9.Mask];
            break;
        case memDomain_ARM7:
            if (nds->ARM7WRAM && address < nds->ARM7WRAMSize)
                return &nds->ARM7WRAM[address];
            break;
    }
    return nullptr;
}

uint32_t MemScanDialog::ReadValue(uint32_t address, ValueType type, bool* valid)
{
    void* ptr = GetRAM(address);
    if (!ptr)
    {
        *valid = false;
        return 0;
    }

    *valid = true;
    switch (type)
    {
        case valueType_Byte:
            return *(uint8_t*)ptr;
        case valueType_Halfword:
            return *(uint16_t*)ptr;
        case valueType_Word:
            return *(uint32_t*)ptr;
    }
    return 0;
}

uint32_t MemScanDialog::GetDomainStart()
{
    switch (currentDomain)
    {
        case memDomain_MainRAM:
            return 0x02000000;
        case memDomain_ARM9:
            return 0x03000000;
        case memDomain_ARM7:
            return 0x03800000;
    }
    return 0;
}

uint32_t MemScanDialog::GetDomainSize()
{
    melonDS::NDS* nds = GetNDS();
    if (!nds) return 0;

    switch (currentDomain)
    {
        case memDomain_MainRAM:
            return nds->MainRAMMask + 1;
        case memDomain_ARM9:
            return nds->SWRAM_ARM9.Mem ? (nds->SWRAM_ARM9.Mask + 1) : 0;
        case memDomain_ARM7:
            return nds->ARM7WRAMSize;
    }
    return 0;
}

uint32_t MemScanDialog::GetValueMask(ValueType type) const
{
    switch (type)
    {
        case valueType_Byte: return 0xFF;
        case valueType_Halfword: return 0xFFFF;
        case valueType_Word: return 0xFFFFFFFF;
    }
    return 0xFFFFFFFF;
}

bool MemScanDialog::ParseSearchValue(uint32_t* value)
{
    QString valueStr = valueEdit->text().trimmed();
    if (valueStr.isEmpty())
    {
        QMessageBox::warning(this, "Invalid Input", "Please enter a value to search for.");
        return false;
    }

    bool ok = false;
    uint32_t parsed = 0;
    if (valueStr.startsWith("0x", Qt::CaseInsensitive))
        parsed = valueStr.mid(2).toUInt(&ok, 16);
    else
        parsed = valueStr.toUInt(&ok, 10);

    if (!ok)
    {
        QMessageBox::warning(this, "Invalid Input", "Invalid value format.");
        return false;
    }

    const uint32_t mask = GetValueMask(currentValueType);
    if ((parsed & ~mask) != 0)
    {
        QMessageBox::warning(
            this,
            "Invalid Input",
            QString("Value exceeds selected type range (max 0x%1).")
                .arg(mask, (currentValueType == valueType_Byte) ? 2 :
                           (currentValueType == valueType_Halfword) ? 4 : 8,
                     16, QChar('0'))
                .toUpper());
        return false;
    }

    *value = parsed;
    return true;
}

void MemScanDialog::onScanTypeChanged(int index)
{
    ScanType scanType = (ScanType)index;
    
    // Value field is only used for exact-value scans
    valueEdit->setEnabled(scanType == scanType_ExactValue);
}

void MemScanDialog::onNewScan()
{
    currentDomain = (MemDomain)domainCombo->currentIndex();
    currentValueType = (ValueType)typeCombo->currentIndex();
    ScanType scanType = (ScanType)scanTypeCombo->currentIndex();
    
    results.clear();
    
    uint32_t searchValue = 0;
    if (scanType == scanType_ExactValue)
    {
        if (!ParseSearchValue(&searchValue))
            return;
    }
    
    uint32_t domainSize = GetDomainSize();
    if (domainSize == 0)
    {
        QMessageBox::warning(this, "Scan Error", "Selected memory domain is not available.");
        return;
    }

    int step = (currentValueType == valueType_Byte) ? 1 : 
               (currentValueType == valueType_Halfword) ? 2 : 4;
    if (domainSize < (uint32_t)step)
    {
        QMessageBox::warning(this, "Scan Error", "Selected memory domain is too small for this value type.");
        return;
    }

    EmuThread* emuThread = nullptr;
    bool pausedForScan = false;
    if (MainWindow* window = qobject_cast<MainWindow*>(this->parentWidget()))
    {
        if (EmuInstance* emuInstance = window->getEmuInstance())
        {
            emuThread = emuInstance->getEmuThread();
            if (emuThread && emuThread->emuIsRunning())
            {
                emuThread->emuPause();
                pausedForScan = true;
            }
        }
    }
    
    // Scan memory
    for (uint32_t addr = 0; addr <= domainSize - step; addr += step)
    {
        bool valid;
        uint32_t value = ReadValue(addr, currentValueType, &valid);
        
        if (!valid)
            continue;
        
        bool matches = false;
        if (scanType == scanType_ExactValue)
        {
            matches = (value == searchValue);
        }
        else if (scanType == scanType_UnknownInitial)
        {
            matches = true; // Store all addresses for unknown initial
        }
        
        if (matches)
        {
            ScanResult result;
            result.address = addr;
            result.value = value;
            result.previousValue = value;
            result.hasPrevious = false;
            results.append(result);
        }
    }

    if (pausedForScan && emuThread)
        emuThread->emuUnpause();
    
    hasInitialScan = true;
    btnNextScan->setEnabled(true);
    btnAddToWatch->setEnabled(!results.isEmpty());

    // Unknown initial is only valid for the first scan pass
    if (scanType == scanType_UnknownInitial)
    {
        QSignalBlocker blocker(scanTypeCombo);
        scanTypeCombo->setCurrentIndex(scanType_UnchangedValue);
    }

    UpdateResultsTable();
}

void MemScanDialog::onNextScan()
{
    if (!hasInitialScan || results.isEmpty())
        return;
    
    ScanType scanType = (ScanType)scanTypeCombo->currentIndex();
    uint32_t searchValue = 0;
    
    if (scanType == scanType_ExactValue)
    {
        if (!ParseSearchValue(&searchValue))
            return;
    }
    else if (scanType == scanType_UnknownInitial)
    {
        QMessageBox::information(this, "Scan Type Not Allowed", "Unknown Initial Value is only available for New Scan.");
        return;
    }

    EmuThread* emuThread = nullptr;
    bool pausedForScan = false;
    if (MainWindow* window = qobject_cast<MainWindow*>(this->parentWidget()))
    {
        if (EmuInstance* emuInstance = window->getEmuInstance())
        {
            emuThread = emuInstance->getEmuThread();
            if (emuThread && emuThread->emuIsRunning())
            {
                emuThread->emuPause();
                pausedForScan = true;
            }
        }
    }
    
    // Filter existing results
    QList<ScanResult> newResults;
    for (const ScanResult& result : results)
    {
        bool valid;
        uint32_t currentValue = ReadValue(result.address, currentValueType, &valid);
        
        if (!valid)
            continue;
        
        bool matches = false;
        switch (scanType)
        {
            case scanType_ExactValue:
                matches = (currentValue == searchValue);
                break;
            case scanType_IncreasedValue:
                matches = (currentValue > result.value);
                break;
            case scanType_DecreasedValue:
                matches = (currentValue < result.value);
                break;
            case scanType_ChangedValue:
                matches = (currentValue != result.value);
                break;
            case scanType_UnchangedValue:
                matches = (currentValue == result.value);
                break;
            default:
                matches = false;
                break;
        }
        
        if (matches)
        {
            ScanResult updated = result;
            updated.previousValue = result.value;
            updated.value = currentValue;
            updated.hasPrevious = true;
            newResults.append(updated);
        }
    }

    if (pausedForScan && emuThread)
        emuThread->emuUnpause();
    
    results = newResults;
    btnAddToWatch->setEnabled(!results.isEmpty());
    UpdateResultsTable();
}

void MemScanDialog::UpdateResultsTable()
{
    resultsTable->setRowCount(0);
    
    // Limit display to first 1000 results
    int displayCount = qMin(results.size(), 1000);
    
    for (int i = 0; i < displayCount; i++)
    {
        const ScanResult& result = results[i];
        
        bool valid;
        uint32_t currentValue = ReadValue(result.address, currentValueType, &valid);
        
        int row = resultsTable->rowCount();
        resultsTable->insertRow(row);
        
        QString addrStr = QString("0x%1").arg(result.address, 8, 16, QChar('0')).toUpper();
        resultsTable->setItem(row, 0, new QTableWidgetItem(addrStr));
        
        if (valid)
        {
            QString valueStr = QString("0x%1").arg(currentValue, 
                (currentValueType == valueType_Byte) ? 2 :
                (currentValueType == valueType_Halfword) ? 4 : 8, 
                16, QChar('0')).toUpper();
            resultsTable->setItem(row, 1, new QTableWidgetItem(valueStr));
        }
        else
        {
            resultsTable->setItem(row, 1, new QTableWidgetItem("INVALID"));
        }
        
        QString prevStr = result.hasPrevious
            ? QString("0x%1").arg(result.previousValue,
                                 (currentValueType == valueType_Byte) ? 2 :
                                 (currentValueType == valueType_Halfword) ? 4 : 8,
                                 16, QChar('0')).toUpper()
            : "---";
        resultsTable->setItem(row, 2, new QTableWidgetItem(prevStr));
    }
    
    QString countStr = QString("Results: %1").arg(results.size());
    if (results.size() > 1000)
        countStr += " (showing first 1000)";
    resultCountLabel->setText(countStr);
}

void MemScanDialog::onAddToWatchList()
{
    int row = resultsTable->currentRow();
    if (row < 0 || row >= results.size())
    {
        QMessageBox::information(this, "No Selection", "Please select a result to add to the watch list.");
        return;
    }
    
    const ScanResult& result = results[row];
    
    // Open or get existing MemWatchDialog
    MemWatchDialog* watchDlg = MemWatchDialog::openDlg(this->parentWidget());
    
    if (watchDlg)
    {
        // Create a watch entry
        WatchEntry entry;
        
        // Calculate absolute address based on domain
        uint32_t absAddress = GetDomainStart() + result.address;
        
        entry.address = absAddress;
        entry.label = QString("Scan 0x%1").arg(absAddress, 8, 16, QChar('0')).toUpper();
        entry.dataType = (WatchDataType)currentValueType;
        entry.displayFormat = watchDisplayFormat_Hex;
        entry.isPointer = false;
        entry.isSeparator = false;
        
        watchDlg->addWatchEntry(entry);
    }
}

void MemScanDialog::done(int r)
{
    QDialog::done(r);
    closeDlg();
}
