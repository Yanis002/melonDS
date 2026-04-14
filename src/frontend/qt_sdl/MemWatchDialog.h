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

#ifndef MEMWATCHDIALOG_H
#define MEMWATCHDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QThread>
#include <QMutex>
#include <QString>
#include <QList>
#include <QDropEvent>

#include "types.h"
#include "NDS.h"

class NDS;
class EmuInstance;
class MemWatchThread;
class MemWatchDialog;

// Custom table widget to handle drag and drop properly
class MemWatchTableWidget : public QTableWidget
{
    Q_OBJECT

public:
    explicit MemWatchTableWidget(MemWatchDialog* parent);
    
protected:
    void dropEvent(QDropEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;
    void keyPressEvent(QKeyEvent* event) override;
    
private:
    MemWatchDialog* dialog;
    int dragSourceRow;
};

enum WatchDataType {
    watchDataType_Byte = 0,
    watchDataType_Halfword = 1,
    watchDataType_Word = 2,
    watchDataType_FourCC = 3,
    watchDataType_String = 4,
};

enum WatchDisplayFormat {
    watchDisplayFormat_Hex = 0,
    watchDisplayFormat_Decimal = 1,
    watchDisplayFormat_Signed = 2,
    watchDisplayFormat_Unsigned = 3,
    watchDisplayFormat_q20Float = 4,
};

struct WatchEntry {
    QString label;
    uint32_t address;
    WatchDataType dataType;
    WatchDisplayFormat displayFormat;
    bool isPointer;
    QList<int32_t> pointerOffsets;
    bool isLastLevelPointer;
    bool isSeparator;
    
    WatchEntry() : label(""), address(0), dataType(watchDataType_Byte), 
                   displayFormat(watchDisplayFormat_Hex), isPointer(false), 
                   isLastLevelPointer(false), isSeparator(false) {}
};

class MemWatchDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MemWatchDialog(QWidget* parent);
    ~MemWatchDialog();

    melonDS::NDS* GetNDS();
    void* GetRAM(uint32_t address);
    uint32_t ReadValue(const WatchEntry& entry, bool* valid);
    void WriteValue(WatchEntry& entry, uint32_t value);
    QString ReadStringValue(const WatchEntry& entry, bool* valid);
    QString ReadFourCCValue(const WatchEntry& entry, bool* valid);
    uint32_t GetEffectiveAddress(const WatchEntry &entry, bool *valid);
    void handleRowMoved(int fromRow, int toRow);
    void RebuildTable();
    void removeCurrentWatch();
    void addWatchEntry(const WatchEntry& entry);

    static MemWatchDialog* currentDlg;
    static MemWatchDialog* openDlg(QWidget* parent)
    {
        if (currentDlg)
        {
            currentDlg->activateWindow();
            return currentDlg;
        }

        currentDlg = new MemWatchDialog(parent);
        currentDlg->show();
        return currentDlg;
    }

    static void closeDlg()
    {
        currentDlg = nullptr;
    }

private slots:
    void done(int r);
    void onAddWatch();
    void onRemoveWatch();
    void onEditWatch();
    void onCellDoubleClicked(int row, int column);
    void onUpdateValuesSignal();
    void onSaveWatches();
    void onLoadWatches();
    void onClearAll();
    void showContextMenu(const QPoint& pos);
    void onLoadRecentFile();
    void onAddSeparatorAbove();
    void onAddSeparatorBelow();

private:
    void UpdateWatchValues();
    void UpdateTableRow(int row);
    void AddWatchToTable(const WatchEntry& entry);
    QString FormatValue(uint32_t value, WatchDisplayFormat format, WatchDataType dataType);
    void LoadWatchFile(const QString& path);
    void AddRecentFile(const QString& path);
    void UpdateRecentFilesMenu();

    static float q20ToFloat(int32_t q20Value) {
        return ((q20Value * 2) - 1) / (float)(1 << 13);
    };

    static int32_t floatToQ20(float floatValue) {
        return (int32_t)((floatValue * (float)(1 << 13) + 1.0f) / 2.0f);
    };

    static QString GetDataTypeString(const WatchDataType& dataType) {
        switch (dataType)
        {
        case watchDataType_Byte:
            return "Byte";
        case watchDataType_Halfword:
            return "Halfword";
        case watchDataType_Word:
            return "Word";
        case watchDataType_String:
            return "String";
        case watchDataType_FourCC:
            return "FourCC";
        default:
            return "Unknown";
        }
    };
    static QString GetDisplayFormatString(const WatchDisplayFormat& displayFormat) {
        switch (displayFormat)
        {
        case watchDisplayFormat_Hex:
            return "Hex";
        case watchDisplayFormat_Decimal:
            return "Decimal";
        case watchDisplayFormat_Signed:
            return "Signed";
        case watchDisplayFormat_Unsigned:
            return "Unsigned";
        case watchDisplayFormat_q20Float:
            return "Q20.12 Float";
        default:
            return "Unknown";
        }
    };

    MemWatchTableWidget* watchTable;
    QMenu* recentFilesMenu;
    
    QList<WatchEntry> watches;
    MemWatchThread* updateThread;
    QMutex watchMutex;

    friend class MemWatchThread;
};

class MemWatchThread : public QThread
{
    Q_OBJECT

public:
    explicit MemWatchThread(MemWatchDialog* parent) : dialog(parent), running(false), paused(false) {}
    ~MemWatchThread() override;

    void Start();
    void Stop();
    void Pause() { paused = true; }
    void Unpause() { paused = false; }

signals:
    void updateValuesSignal();

private:
    void run() override;
    
    MemWatchDialog* dialog;
    bool running;
    bool paused;
};

#endif // MEMWATCHDIALOG_H
