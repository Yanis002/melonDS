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

#ifndef MEMSCANDIALOG_H
#define MEMSCANDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QString>
#include <QList>

#include "NDS.h"

class EmuInstance;

enum MemDomain {
    memDomain_MainRAM = 0,
    memDomain_ARM9 = 1,
    memDomain_ARM7 = 2
};

enum ScanType {
    scanType_ExactValue = 0,
    scanType_UnknownInitial = 1,
    scanType_IncreasedValue = 2,
    scanType_DecreasedValue = 3,
    scanType_ChangedValue = 4,
    scanType_UnchangedValue = 5
};

enum ValueType {
    valueType_Byte = 0,
    valueType_Halfword = 1,
    valueType_Word = 2
};

struct ScanResult {
    uint32_t address;
    uint32_t value;
    uint32_t previousValue;
    bool hasPrevious;
};

class MemScanDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MemScanDialog(QWidget* parent);
    ~MemScanDialog();

    melonDS::NDS* GetNDS();
    void* GetRAM(uint32_t address);
    uint32_t ReadValue(uint32_t address, ValueType type, bool* valid);

    static MemScanDialog* currentDlg;
    static MemScanDialog* openDlg(QWidget* parent)
    {
        if (currentDlg)
        {
            currentDlg->activateWindow();
            return currentDlg;
        }

        currentDlg = new MemScanDialog(parent);
        currentDlg->show();
        return currentDlg;
    }

    static void closeDlg()
    {
        currentDlg = nullptr;
    }

private slots:
    void done(int r);
    void onNewScan();
    void onNextScan();
    void onAddToWatchList();
    void onScanTypeChanged(int index);

private:
    void UpdateResultsTable();
    bool ParseSearchValue(uint32_t* value);
    uint32_t GetDomainStart();
    uint32_t GetDomainSize();
    uint32_t GetValueMask(ValueType type) const;
    
    QComboBox* domainCombo;
    QComboBox* typeCombo;
    QComboBox* scanTypeCombo;
    QLineEdit* valueEdit;
    QPushButton* btnNewScan;
    QPushButton* btnNextScan;
    QPushButton* btnAddToWatch;
    QTableWidget* resultsTable;
    QLabel* resultCountLabel;
    
    QList<ScanResult> results;
    ValueType currentValueType;
    MemDomain currentDomain;
    bool hasInitialScan;
};

#endif // MEMSCANDIALOG_H
