#include "MemWatchActor.h"

#include <QListWidgetItem>
#include <QVBoxLayout>
#include <QLabel>
#include <QDebug>
#include <cstring>
#include <algorithm>
#include <QMenu>
#include <QMouseEvent>

#include "types.h"
#include "NDS.h"
#include "Window.h"
#include "EmuInstance.h"

using namespace melonDS;

MemWatchActor::MemWatchActor(QWidget *parent)
    : QListWidget(parent)
    , m_updateTimer(nullptr)
    , m_manualMode(false)
{
    setWindowTitle("Actor Manager");
    
    // Set consistent font for better readability
    QFont monospaceFont("Courier", 9);
    setFont(monospaceFont);
    
    // Set reasonable size hints
    setMinimumHeight(200);
    
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &MemWatchActor::updateActors);
    m_updateTimer->start(100);
    
    // Connect item click for kill/revive checkboxes
    connect(this, &QListWidget::itemClicked, this, &MemWatchActor::onItemClicked);
    
    // Enable context menu for teleport functionality
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &MemWatchActor::onContextMenu);
}

MemWatchActor::~MemWatchActor()
{
    if (m_updateTimer)
    {
        m_updateTimer->stop();
        delete m_updateTimer;
    }
}

void MemWatchActor::setThrottleEnabled(bool enabled)
{
    if (enabled)
    {
        m_updateTimer->start();
    }
    else
    {
        m_updateTimer->stop();
    }
}

void MemWatchActor::setThrottleInterval(int milliseconds)
{
    if (m_updateTimer)
    {
        m_updateTimer->setInterval(milliseconds);
        if (m_updateTimer->isActive())
        {
            m_updateTimer->start(); // Restart with new interval
        }
    }
}

void MemWatchActor::manualRefresh()
{
    updateActors();
}

void MemWatchActor::setManualMode(bool manual)
{
    m_manualMode = manual;
    qDebug() << "Manual mode set to:" << manual;
}

void MemWatchActor::toggleActorKill(int row, bool kill)
{
    QListWidgetItem *item = this->item(row);
    if (!item) {
        qDebug() << "Item not found for row" << row;
        return;
    }

    uint32_t actorAddress = item->data(Qt::UserRole).toUInt();
    qDebug() << "toggleActorKill - row:" << row << "kill:" << kill << "actorAddress:" << QString::number(actorAddress, 16);
    
    if (!actorAddress) {
        qDebug() << "No actor address stored";
        return;
    }

    if (kill)
    {
        // Store original address and write 0x0
        void *addrPtr = GetRAM(actorAddress);
        if (!addrPtr) {
            qDebug() << "Failed to get RAM at address" << QString::number(actorAddress, 16);
            return;
        }
        
        uint32_t originalAddress = *(uint32_t *)addrPtr;
        qDebug() << "Storing original address:" << QString::number(originalAddress, 16);
        m_killedActors[row] = originalAddress;
        
        uint32_t zero = 0;
        bool success = SetRAM(actorAddress, &zero, 4);
        qDebug() << "SetRAM returned:" << success;
    }
    else if (m_killedActors.contains(row))
    {
        // Restore original address
        uint32_t originalAddress = m_killedActors[row];
        qDebug() << "Restoring address:" << QString::number(originalAddress, 16);
        bool success = SetRAM(actorAddress, &originalAddress, 4);
        qDebug() << "SetRAM restore returned:" << success;
        m_killedActors.remove(row);
    }
}

void MemWatchActor::mousePressEvent(QMouseEvent *event)
{
    QListWidgetItem *item = itemAt(event->pos());
    
    if (event->button() == Qt::LeftButton && item)
    {
        if (m_manualMode)
        {
            int row = this->row(item);
            uint32_t actorAddress = item->data(Qt::UserRole).toUInt();
            bool isKilled = m_killedActors.contains(row);
            
            qDebug() << "Left-click on row:" << row << "Address:" << QString::number(actorAddress, 16) << "Already killed:" << isKilled;
            
            // Toggle kill state
            toggleActorKill(row, !isKilled);
            
            qDebug() << "After toggle, killed map contains row:" << m_killedActors.contains(row);
            
            // Update visual feedback
            if (m_killedActors.contains(row))
            {
                // Mark as killed (grayed out)
                item->setForeground(QColor(128, 128, 128));
                QString currentText = item->text();
                if (!currentText.endsWith(" [KILLED]"))
                    item->setText(currentText + " [KILLED]");
            }
            else
            {
                // Restore normal appearance
                item->setForeground(QColor(0, 0, 0));
                QString text = item->text();
                if (text.endsWith(" [KILLED]"))
                    text.chop(9);
                item->setText(text);
            }
        }
    }
    
    QListWidget::mousePressEvent(event);
}

void MemWatchActor::onItemClicked(QListWidgetItem *item)
{
    // Click handling is now done in mousePressEvent to differentiate left vs right clicks
}

void MemWatchActor::onContextMenu(const QPoint &pos)
{
    if (!m_manualMode)
        return;

    QListWidgetItem *item = itemAt(pos);
    if (!item)
        return;

    QMenu contextMenu;
    QAction *teleportAction = contextMenu.addAction("Teleport Link here");
    
    QAction *selectedAction = contextMenu.exec(mapToGlobal(pos));
    if (selectedAction == teleportAction)
    {
        int row = this->row(item);
        teleportLinkToActor(row);
    }
}

void MemWatchActor::teleportLinkToActor(int row)
{
    QListWidgetItem *item = this->item(row);
    if (!item)
    {
        qDebug() << "Item not found for row" << row;
        return;
    }

    uint32_t tableEntryAddress = item->data(Qt::UserRole).toUInt();
    if (!tableEntryAddress)
    {
        qDebug() << "No actor address stored";
        return;
    }

    // Read actor position from the table entry (it's a pointer to the actor)
    void *actorPtrPtr = GetRAM(tableEntryAddress);
    if (!actorPtrPtr)
    {
        qDebug() << "Failed to get actor pointer at address" << QString::number(tableEntryAddress, 16);
        return;
    }

    uint32_t actorAddress = *(uint32_t *)actorPtrPtr;
    if (!actorAddress)
    {
        qDebug() << "Actor address is null";
        return;
    }

    // Read actor position (offset 0x04, 3 x int32_t)
    void *posPtr = GetRAM(actorAddress + 0x04);
    if (!posPtr)
    {
        qDebug() << "Failed to get actor position at address" << QString::number(actorAddress + 0x04, 16);
        return;
    }

    int32_t *pos = (int32_t *)posPtr;
    int32_t posX = pos[0];
    int32_t posY = pos[1];
    int32_t posZ = pos[2];

    // Write Link's position to LINK_POSITION_ADDRESS
    void *linkPosPtr = GetRAM(LINK_POSITION_ADDRESS);
    if (!linkPosPtr)
    {
        qDebug() << "Failed to get Link position address" << QString::number(LINK_POSITION_ADDRESS, 16);
        return;
    }

    int32_t *linkPos = (int32_t *)linkPosPtr;
    linkPos[0] = posX;
    linkPos[1] = posY;
    linkPos[2] = posZ;

    qDebug() << "Teleported Link to actor position:" << posX << posY << posZ;
}

melonDS::NDS *MemWatchActor::GetNDS()
{
    // Walk up the parent chain to find the MainWindow
    QWidget *p = parentWidget();
    while (p)
    {
        MainWindow *mainWindow = qobject_cast<MainWindow *>(p);
        if (mainWindow)
        {
            EmuInstance *emuInstance = mainWindow->getEmuInstance();
            if (emuInstance)
            {
                return emuInstance->getNDS();
            }
            return nullptr;
        }
        p = p->parentWidget();
    }
    return nullptr;
}


// DO NOT TOUCH
void *MemWatchActor::GetRAM(uint32_t address)
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

bool MemWatchActor::SetRAM(uint32_t address, const void *data, int size)
{
    melonDS::NDS *nds = this->GetNDS();
    if (!nds || size != 4)
        return false;

    void *ptr = nullptr;

    if (address < nds->ARM9.ITCMSize)
    {
        ptr = &nds->ARM9.ITCM[address & (ITCMPhysicalSize - 1)];
    }
    else if (nds->ARM9.DTCM != nullptr && (address & nds->ARM9.DTCMMask) == nds->ARM9.DTCMBase)
    {
        ptr = &nds->ARM9.DTCM[address & (DTCMPhysicalSize - 1)];
    }
    else if ((address & 0xFF000000) == 0x02000000)
    {
        if (nds->MainRAM != nullptr)
            ptr = &nds->MainRAM[address & nds->MainRAMMask];
    }
    else if ((address & 0xFF000000) == 0x03000000)
    {
        if (nds->SWRAM_ARM9.Mem != nullptr)
            ptr = &nds->SWRAM_ARM9.Mem[address & nds->SWRAM_ARM9.Mask];
    }

    if (ptr)
    {
        *(uint32_t *)ptr = *(const uint32_t *)data;
        return true;
    }
    return false;
}

// DO NOT TOUCH
uint32_t MemWatchActor::GetEffectiveAddress(uint32_t baseAddress, uint32_t offsets[], bool *valid, bool isLastOffsetPointer)
{
  *valid = false;
  uint32_t address = baseAddress;

  // If it's not a pointer, the base address is the final destination
  if (!isLastOffsetPointer)
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
  // Count offsets until we hit a null terminator or offset limit
  int offsetCount = 0;
  while (offsets[offsetCount] != 0 || offsetCount == 0)
  {
    offsetCount++;
    if (offsetCount > 100) break; // Safety limit
  }

  for (int i = 0; i < offsetCount; i++)
  {
    int32_t offset = offsets[i];
    bool isLastOffset = (i == offsetCount - 1);

    // Apply the offset to the value we read
    address = ptrValue + offset;

    // If it's the last offset and it's NOT a pointer, we found our final address
    if (isLastOffset && !isLastOffsetPointer)
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
  if (isLastOffsetPointer)
  {
    address = ptrValue;
  }

  *valid = true;
  return address;
}

QString MemWatchActor::actorIdToFourCC(uint32_t actorId)
{
    // Little endian: bytes are stored LSB first
    uint8_t bytes[4];
    bytes[3] = (actorId >> 0) & 0xFF;
    bytes[2] = (actorId >> 8) & 0xFF;
    bytes[1] = (actorId >> 16) & 0xFF;
    bytes[0] = (actorId >> 24) & 0xFF;
    
    QString result;
    for (int i = 0; i < 4; ++i)
    {
        char c = bytes[i];
        // Check if character is printable ASCII
        if (c >= 32 && c <= 126)
        {
            result += c;
        }
        else
        {
            result += '?';
        }
    }
    return result;
}

float MemWatchActor::q20ToFloat(int32_t q20Value)
{
    return ((q20Value * 2) - 1) / 8192.f;

}

ActorData MemWatchActor::readActorData(uint32_t actorAddress)
{
    ActorData data;
    data.address = actorAddress;
    data.posX = 0;
    data.posY = 0;
    data.posZ = 0;
    data.velX = 0;
    data.velY = 0;
    data.velZ = 0;
    data.flags = 0;
    data.actorId = 0;
    data.actorIdString = "????";

    if (!actorAddress)
        return data;

    // Read mPos (offset 0x04, 3 x s32)
    void *posPtr = GetRAM(actorAddress + 0x04);
    if (posPtr)
    {
        int32_t *pos = (int32_t *)posPtr;
        data.posX = pos[0];
        data.posY = pos[1];
        data.posZ = pos[2];
    }

    // Read mVel (offset 0x1c, 3 x s32)
    void *velPtr = GetRAM(actorAddress + 0x1c);
    if (velPtr)
    {
        int32_t *vel = (int32_t *)velPtr;
        data.velX = vel[0];
        data.velY = vel[1];
        data.velZ = vel[2];
    }

    // Read mFlags (offset 0x58, u32)
    void *flagsPtr = GetRAM(actorAddress + 0x58);
    if (flagsPtr)
    {
        data.flags = *(uint32_t *)flagsPtr;
    }

    // Read mType pointer (offset 0x90, pointer to ActorType)
    void *typePtr = GetRAM(actorAddress + 0x90);
    if (typePtr)
    {
        uint32_t actorTypeAddr = *(uint32_t *)typePtr;
        if (actorTypeAddr)
        {
            // Read mActorId from ActorType (offset 0x20, u32)
            void *actorIdPtr = GetRAM(actorTypeAddr + 0x20);
            if (actorIdPtr)
            {
                data.actorId = *(uint32_t *)actorIdPtr;
                data.actorIdString = actorIdToFourCC(data.actorId);
            }
        }
    }

    return data;
}

void MemWatchActor::refreshActorList()
{
    // Get NDS first
    melonDS::NDS *nds = GetNDS();
    if (!nds)
    {
        clear();
        return;
    }

    // Clear killed actors map since we're refreshing (they might be gone)
    m_killedActors.clear();

    // The fixed address is a pointer to the ActorManager
    void *managerPtrPtr = GetRAM(ACTOR_MANAGER_ADDRESS);
    if (!managerPtrPtr)
    {
        clear();
        return;
    }

    // Dereference to get the actual ActorManager address
    uint32_t managerAddress = *(uint32_t *)managerPtrPtr;
    if (!managerAddress)
    {
        clear();
        return;
    }

    // Read mActorTable and mActorTableEnd from ActorManager
    // mActorTable at offset 0x00 (pointer)
    // mActorTableEnd at offset 0x04 (pointer)
    void *tablePtr = GetRAM(managerAddress + 0x00);
    void *tableEndPtr = GetRAM(managerAddress + 0x04);

    if (!tablePtr || !tableEndPtr)
    {
        clear();
        return;
    }

    uint32_t actorTableAddr = *(uint32_t *)tablePtr;
    uint32_t actorTableEndAddr = *(uint32_t *)tableEndPtr;

    if (!actorTableAddr || !actorTableEndAddr)
    {
        clear();
        return;
    }

    // Read actor pointers from the table
    QMap<uint32_t, ActorData> newActors;
    int actorCount = 0;

    for (uint32_t tableOffset = 0; actorCount < MAX_ACTORS; tableOffset += 4)
    {
        uint32_t currentAddr = actorTableAddr + tableOffset;
        
        // Stop when we reach the end of the table
        if (currentAddr >= actorTableEndAddr)
            break;

        void *actorPtrPtr = GetRAM(currentAddr);
        if (!actorPtrPtr)
            break;

        uint32_t actorAddr = *(uint32_t *)actorPtrPtr;
        if (!actorAddr)
            continue;

        // Read actor data
        ActorData actorData = readActorData(actorAddr);
        actorData.tableEntryAddress = currentAddr;  // Store the table entry address
        newActors[actorAddr] = actorData;
        actorCount++;
    }

    // Update list widget with changed actors
    m_cachedActors = newActors;

    clear();

    // Update parent dialog title with actor count
    QWidget *parentWidget = this->parentWidget();
    if (parentWidget)
    {
        parentWidget->setWindowTitle(QString("Actor Explorer - %1 actors").arg(newActors.size()));
    }

    if (newActors.isEmpty())
    {
        return;
    }

    // Convert to QList and sort by actorIdString
    QList<ActorData> sortedActors = newActors.values();
    std::sort(sortedActors.begin(), sortedActors.end(), 
        [](const ActorData &a, const ActorData &b) {
            return a.actorIdString < b.actorIdString;
        });

    // Add actors to list with formatted display
    for (const ActorData &actor : sortedActors)
    {
        float posX = q20ToFloat(actor.posX);
        float posY = q20ToFloat(actor.posY);
        float posZ = q20ToFloat(actor.posZ);
        float velX = q20ToFloat(actor.velX);
        float velY = q20ToFloat(actor.velY);
        float velZ = q20ToFloat(actor.velZ);
        
        QString displayText = QString("[%1]  Pos:(%2,%3,%4)  Vel:(%5,%6,%7)  F:0x%8")
            .arg(actor.actorIdString, -4)
            .arg(posX, 7, 'f', 1)
            .arg(posY, 7, 'f', 1)
            .arg(posZ, 7, 'f', 1)
            .arg(velX, 7, 'f', 1)
            .arg(velY, 7, 'f', 1)
            .arg(velZ, 7, 'f', 1)
            .arg(actor.flags, 8, 16, QChar('0'));

        QListWidgetItem *item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, actor.tableEntryAddress);  // Store table entry address for kill/revive
        addItem(item);
    }
}

void MemWatchActor::updateActors()
{
    refreshActorList();
}