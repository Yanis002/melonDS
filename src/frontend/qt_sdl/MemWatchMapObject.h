#ifndef MEMWATCHMAPOBJECT_H
#define MEMWATCHMAPOBJECT_H

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

// MapObject data structure for caching
struct MapObjectData
{
    uint32_t address;  // The mapobject object address
    uint32_t tableEntryAddress;  // Address of the table entry pointing to this mapobject
    int32_t posX, posY, posZ;
    uint32_t flags;
    uint32_t mapObjectId; // ID from MapObjectProfile
    QString mapObjectIdString; // String representation
    
    bool operator==(const MapObjectData& other) const
    {
        return address == other.address &&
               posX == other.posX && posY == other.posY && posZ == other.posZ &&
               flags == other.flags && mapObjectId == other.mapObjectId;
    }
};

// Custom list widget to display mapobjects from MapObjectManager
class MemWatchMapObject : public QListWidget
{
    Q_OBJECT

public:
    explicit MemWatchMapObject(QWidget *parent = nullptr);
    ~MemWatchMapObject();
    melonDS::NDS *GetNDS();
    void *GetRAM(uint32_t address);
    bool SetRAM(uint32_t address, const void *data, int size);
    
    // Control methods for throttle
    void setThrottleEnabled(bool enabled);
    void setThrottleInterval(int milliseconds);
    void manualRefresh();
    void setManualMode(bool manual);
    
    // Kill/revive functionality
    void toggleMapObjectKill(int row, bool kill);
    
    // Teleport functionality
    void teleportLinkToMapObject(int row);

protected:
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void updateMapObjects();
    void onItemClicked(QListWidgetItem *item);
    void onContextMenu(const QPoint &pos);

private:
    // DO NOT TOUCH
    uint32_t GetEffectiveAddress(uint32_t baseAddress, uint32_t offsets[], bool* valid, bool isLastOffsetPointer = false);

    // MapObject reading functions
    MapObjectData readMapObjectData(uint32_t mapobjectAddress);
    QString mapObjectIdToString(uint32_t mapObjectId);
    float q20ToFloat(int32_t q20Value);
    void refreshMapObjectList();
    
    QTimer *m_updateTimer;
    QMap<uint32_t, MapObjectData> m_cachedMapObjects;
    QMap<uint32_t, uint32_t> m_killedMapObjects; // Maps mapobject address to saved address value
    bool m_manualMode;
    
    static const uint32_t MAPOBJECT_MANAGER_ADDRESS = 0x027e0ce8;
    static const uint32_t LINK_POSITION_ADDRESS = 0x027e05cc;
    static const int MAX_MAPOBJECTS = 512; // Safety limit for iteration
};

#endif // MEMWATCHMAPOBJECT_H
