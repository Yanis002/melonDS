#ifndef MEMWATCHACTOR_H
#define MEMWATCHACTOR_H

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

// Actor data structure for caching
struct ActorData
{
    uint32_t address;  // The actor object address
    uint32_t tableEntryAddress;  // Address of the table entry pointing to this actor
    int32_t posX, posY, posZ;
    int32_t velX, velY, velZ;
    uint32_t flags;
    uint32_t actorId; // Full 4-byte ID
    QString actorIdString; // FourCC representation
    
    bool operator==(const ActorData& other) const
    {
        return address == other.address &&
               posX == other.posX && posY == other.posY && posZ == other.posZ &&
               velX == other.velX && velY == other.velY && velZ == other.velZ &&
               flags == other.flags && actorId == other.actorId;
    }
};

// Custom list widget to display actors from ActorManager
class MemWatchActor : public QListWidget
{
    Q_OBJECT

public:
    explicit MemWatchActor(QWidget *parent = nullptr);
    ~MemWatchActor();
    melonDS::NDS *GetNDS();
    void *GetRAM(uint32_t address);
    bool SetRAM(uint32_t address, const void *data, int size);
    
    // Control methods for throttle
    void setThrottleEnabled(bool enabled);
    void setThrottleInterval(int milliseconds);
    void manualRefresh();
    void setManualMode(bool manual);
    
    // Kill/revive functionality
    void toggleActorKill(int row, bool kill);
    
    // Teleport functionality
    void teleportLinkToActor(int row);

protected:
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void updateActors();
    void onItemClicked(QListWidgetItem *item);
    void onContextMenu(const QPoint &pos);

private:
    // DO NOT TOUCH
    uint32_t GetEffectiveAddress(uint32_t baseAddress, uint32_t offsets[], bool* valid, bool isLastOffsetPointer = false);

    // Actor reading functions
    ActorData readActorData(uint32_t actorAddress);
    QString actorIdToFourCC(uint32_t actorId);
    float q20ToFloat(int32_t q20Value);
    void refreshActorList();
    
    QTimer *m_updateTimer;
    QMap<uint32_t, ActorData> m_cachedActors;
    QMap<uint32_t, uint32_t> m_killedActors; // Maps actor address to saved address value
    bool m_manualMode;
    
    static const uint32_t ACTOR_MANAGER_ADDRESS = 0x027e0ce4;
    static const uint32_t LINK_POSITION_ADDRESS = 0x027e05cc;
    static const int MAX_ACTORS = 512; // Safety limit for iteration
};

#endif // MEMWATCHACTOR_H