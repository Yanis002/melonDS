#ifndef MEMWATCHACTORDIALOG_H
#define MEMWATCHACTORDIALOG_H

#include <QDialog>
#include "MemWatchActor.h"

class QCheckBox;
class QComboBox;
class QPushButton;

class MemWatchActorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MemWatchActorDialog(QWidget* parent);
    ~MemWatchActorDialog();

    static MemWatchActorDialog* currentDlg;
    static MemWatchActorDialog* openDlg(QWidget* parent)
    {
        if (currentDlg)
        {
            currentDlg->activateWindow();
            return currentDlg;
        }

        currentDlg = new MemWatchActorDialog(parent);
        currentDlg->show();
        return currentDlg;
    }

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onThrottledToggled(bool checked);
    void onThrottleValueChanged(int index);
    void onManualSearch();

private:
    MemWatchActor* m_actorWidget;
    QCheckBox* m_throttledCheckbox;
    QComboBox* m_throttleComboBox;
    QPushButton* m_searchButton;
};

#endif // MEMWATCHACTORDIALOG_H
