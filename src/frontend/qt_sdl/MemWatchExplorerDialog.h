#ifndef MEMWATCHEXPLORERDIALOG_H
#define MEMWATCHEXPLORERDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QTimer>
#include <QMap>
#include <QString>
#include <QList>

#include "types.h"
#include "NDS.h"

class NDS;
class EmuInstance;
class MainWindow;
class QCheckBox;
class QComboBox;
class QPushButton;
class QTabWidget;

// ============ BASE CLASS ============

class MemWatchExplorer : public QListWidget
{
    Q_OBJECT

public:
    explicit MemWatchExplorer(QWidget *parent = nullptr);
    virtual ~MemWatchExplorer();
    melonDS::NDS *GetNDS();
    void *GetRAM(uint32_t address);
    bool SetRAM(uint32_t address, const void *data, int size);
    
    void setThrottleEnabled(bool enabled);
    void setThrottleInterval(int milliseconds);
    void manualRefresh();
    void setManualMode(bool manual);
    
    virtual void toggleKill(int row, bool kill) = 0;
    virtual void teleportLinkToObject(int row) = 0;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    
    virtual void refreshList() = 0;
    virtual QString getObjectIdString(uint32_t id) = 0;
    virtual void displayObject(const QString &displayText, uint32_t tableEntryAddress) = 0;
    
    float q20ToFloat(int32_t q20Value);
    
    uint32_t GetEffectiveAddress(uint32_t baseAddress, uint32_t offsets[], bool* valid, bool isLastOffsetPointer = false);

    QTimer *m_updateTimer;
    QMap<uint32_t, uint32_t> m_killedObjects;
    bool m_manualMode;
    
    static const uint32_t LINK_POSITION_ADDRESS = 0x027e05cc;

private slots:
    void updateObjects();
    void onItemClicked(QListWidgetItem *item);
    void onContextMenu(const QPoint &pos);
};

// ============ ACTOR DATA STRUCT ============

struct ActorData
{
    uint32_t address;
    uint32_t tableEntryAddress;
    int32_t posX, posY, posZ;
    int32_t velX, velY, velZ;
    uint32_t flags;
    uint32_t actorId;
    QString actorIdString;
    
    bool operator==(const ActorData& other) const
    {
        return address == other.address &&
               posX == other.posX && posY == other.posY && posZ == other.posZ &&
               velX == other.velX && velY == other.velY && velZ == other.velZ &&
               flags == other.flags && actorId == other.actorId;
    }
};

// ============ ACTOR EXPLORER WIDGET ============

class MemWatchActor : public MemWatchExplorer
{
    Q_OBJECT

public:
    explicit MemWatchActor(QWidget *parent = nullptr);
    ~MemWatchActor();
    
    void toggleActorKill(int row, bool kill);
    void teleportLinkToActor(int row);

protected:
    void refreshList() override;
    QString getObjectIdString(uint32_t id) override;
    void displayObject(const QString &displayText, uint32_t tableEntryAddress) override;
    void toggleKill(int row, bool kill) override;
    void teleportLinkToObject(int row) override;

private:
    ActorData readActorData(uint32_t actorAddress);
    QString actorIdToFourCC(uint32_t actorId);
    
    QMap<uint32_t, ActorData> m_cachedActors;
    
    static const uint32_t ACTOR_MANAGER_ADDRESS = 0x027e0ce4;
    static const int MAX_ACTORS = 512;
};

// ============ MAPOBJECT DATA STRUCT ============

struct MapObjectData
{
    uint32_t address;
    uint32_t tableEntryAddress;
    int32_t posX, posY, posZ;
    uint32_t flags;
    uint32_t mapObjectId;
    QString mapObjectIdString;
    
    bool operator==(const MapObjectData& other) const
    {
        return address == other.address &&
               posX == other.posX && posY == other.posY && posZ == other.posZ &&
               flags == other.flags && mapObjectId == other.mapObjectId;
    }
};

// ============ MAPOBJECT EXPLORER WIDGET ============

class MemWatchMapObject : public MemWatchExplorer
{
    Q_OBJECT

public:
    explicit MemWatchMapObject(QWidget *parent = nullptr);
    ~MemWatchMapObject();
    
    void toggleMapObjectKill(int row, bool kill);
    void teleportLinkToMapObject(int row);

protected:
    void refreshList() override;
    QString getObjectIdString(uint32_t id) override;
    void displayObject(const QString &displayText, uint32_t tableEntryAddress) override;
    void toggleKill(int row, bool kill) override;
    void teleportLinkToObject(int row) override;

private:
    MapObjectData readMapObjectData(uint32_t mapobjectAddress);
    QString mapObjectIdToString(uint32_t mapObjectId);
    
    QMap<uint32_t, MapObjectData> m_cachedMapObjects;
    
    static const uint32_t MAPOBJECT_MANAGER_ADDRESS = 0x027e0ce8;
    static const int MAX_MAPOBJECTS = 512;
};

// ============ EXPLORER DIALOG ============

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
    void onTabChanged(int index);

private:
    QTabWidget* m_tabWidget;
    MemWatchActor* m_actorWidget;
    MemWatchMapObject* m_mapObjectWidget;
    QCheckBox* m_throttledCheckbox;
    QComboBox* m_throttleComboBox;
    QPushButton* m_searchButton;
};

#endif // MEMWATCHEXPLORERDIALOG_H
