#include "MemWatchMapObject.h"

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

MemWatchMapObject::MemWatchMapObject(QWidget *parent)
    : QListWidget(parent)
    , m_updateTimer(nullptr)
    , m_manualMode(false)
{
    setWindowTitle("MapObject Manager");
    
    // Set consistent font for better readability
    QFont monospaceFont("Courier", 9);
    setFont(monospaceFont);
    
    // Set reasonable size hints
    setMinimumHeight(200);
    
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &MemWatchMapObject::updateMapObjects);
    m_updateTimer->start(100);
    
    // Connect item click for kill/revive checkboxes
    connect(this, &QListWidget::itemClicked, this, &MemWatchMapObject::onItemClicked);
    
    // Enable context menu for teleport functionality
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &MemWatchMapObject::onContextMenu);
}

MemWatchMapObject::~MemWatchMapObject()
{
    if (m_updateTimer)
    {
        m_updateTimer->stop();
        delete m_updateTimer;
    }
}

void MemWatchMapObject::setThrottleEnabled(bool enabled)
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

void MemWatchMapObject::setThrottleInterval(int milliseconds)
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

void MemWatchMapObject::manualRefresh()
{
    updateMapObjects();
}

void MemWatchMapObject::setManualMode(bool manual)
{
    m_manualMode = manual;
    qDebug() << "Manual mode set to:" << manual;
}

void MemWatchMapObject::toggleMapObjectKill(int row, bool kill)
{
    QListWidgetItem *item = this->item(row);
    if (!item) {
        qDebug() << "Item not found for row" << row;
        return;
    }

    uint32_t mapobjectAddress = item->data(Qt::UserRole).toUInt();
    qDebug() << "toggleMapObjectKill - row:" << row << "kill:" << kill << "mapobjectAddress:" << QString::number(mapobjectAddress, 16);
    
    if (!mapobjectAddress) {
        qDebug() << "No mapobject address stored";
        return;
    }

    if (kill)
    {
        // Store original address and write 0x0
        void *addrPtr = GetRAM(mapobjectAddress);
        if (!addrPtr) {
            qDebug() << "Failed to get RAM at address" << QString::number(mapobjectAddress, 16);
            return;
        }
        
        uint32_t originalAddress = *(uint32_t *)addrPtr;
        qDebug() << "Storing original address:" << QString::number(originalAddress, 16);
        m_killedMapObjects[row] = originalAddress;
        
        uint32_t zero = 0;
        bool success = SetRAM(mapobjectAddress, &zero, 4);
        qDebug() << "SetRAM returned:" << success;
    }
    else if (m_killedMapObjects.contains(row))
    {
        // Restore original address
        uint32_t originalAddress = m_killedMapObjects[row];
        qDebug() << "Restoring address:" << QString::number(originalAddress, 16);
        bool success = SetRAM(mapobjectAddress, &originalAddress, 4);
        qDebug() << "SetRAM restore returned:" << success;
        m_killedMapObjects.remove(row);
    }
}

void MemWatchMapObject::mousePressEvent(QMouseEvent *event)
{
    QListWidgetItem *item = itemAt(event->pos());
    
    if (event->button() == Qt::LeftButton && item)
    {
        if (m_manualMode)
        {
            int row = this->row(item);
            uint32_t mapobjectAddress = item->data(Qt::UserRole).toUInt();
            bool isKilled = m_killedMapObjects.contains(row);
            
            qDebug() << "Left-click on row:" << row << "Address:" << QString::number(mapobjectAddress, 16) << "Already killed:" << isKilled;
            
            // Toggle kill state
            toggleMapObjectKill(row, !isKilled);
            
            qDebug() << "After toggle, killed map contains row:" << m_killedMapObjects.contains(row);
            
            // Update visual feedback
            if (m_killedMapObjects.contains(row))
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

void MemWatchMapObject::onItemClicked(QListWidgetItem *item)
{
    // Click handling is now done in mousePressEvent to differentiate left vs right clicks
}

void MemWatchMapObject::onContextMenu(const QPoint &pos)
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
        teleportLinkToMapObject(row);
    }
}

void MemWatchMapObject::teleportLinkToMapObject(int row)
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
        qDebug() << "No mapobject address stored";
        return;
    }

    // Read mapobject address from the table entry (it's a pointer to the mapobject)
    void *mapobjectPtrPtr = GetRAM(tableEntryAddress);
    if (!mapobjectPtrPtr)
    {
        qDebug() << "Failed to get mapobject pointer at address" << QString::number(tableEntryAddress, 16);
        return;
    }

    uint32_t mapobjectAddress = *(uint32_t *)mapobjectPtrPtr;
    if (!mapobjectAddress)
    {
        qDebug() << "MapObject address is null";
        return;
    }

    // Read mapobject position (offset 0x04, 3 x int32_t)
    void *posPtr = GetRAM(mapobjectAddress + 0x04);
    if (!posPtr)
    {
        qDebug() << "Failed to get mapobject position at address" << QString::number(mapobjectAddress + 0x04, 16);
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

    qDebug() << "Teleported Link to mapobject position:" << posX << posY << posZ;
}

melonDS::NDS *MemWatchMapObject::GetNDS()
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
void *MemWatchMapObject::GetRAM(uint32_t address)
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

bool MemWatchMapObject::SetRAM(uint32_t address, const void *data, int size)
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
uint32_t MemWatchMapObject::GetEffectiveAddress(uint32_t baseAddress, uint32_t offsets[], bool *valid, bool isLastOffsetPointer)
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

QString MemWatchMapObject::mapObjectIdToString(uint32_t mapObjectId)
{
    // Little endian: bytes are stored LSB first
    uint8_t bytes[4];
    bytes[3] = (mapObjectId >> 0) & 0xFF;
    bytes[2] = (mapObjectId >> 8) & 0xFF;
    bytes[1] = (mapObjectId >> 16) & 0xFF;
    bytes[0] = (mapObjectId >> 24) & 0xFF;
    
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

float MemWatchMapObject::q20ToFloat(int32_t q20Value)
{
    return ((q20Value * 2) - 1) / 8192.f;
}

MapObjectData MemWatchMapObject::readMapObjectData(uint32_t mapobjectAddress)
{
    MapObjectData data;
    data.address = mapobjectAddress;
    data.posX = 0;
    data.posY = 0;
    data.posZ = 0;
    data.flags = 0;
    data.mapObjectId = 0;
    data.mapObjectIdString = "????";

    if (!mapobjectAddress)
        return data;

    // Read mPos (offset 0x04, 3 x s32)
    void *posPtr = GetRAM(mapobjectAddress + 0x04);
    if (posPtr)
    {
        int32_t *pos = (int32_t *)posPtr;
        data.posX = pos[0];
        data.posY = pos[1];
        data.posZ = pos[2];
    }

    // Read mFlags (offset 0x1C, u32)
    void *flagsPtr = GetRAM(mapobjectAddress + 0x1C);
    if (flagsPtr)
    {
        data.flags = *(uint32_t *)flagsPtr;
    }

    // Read mpProfile pointer (offset 0x3C, pointer to MapObjectProfile)
    void *profilePtr = GetRAM(mapobjectAddress + 0x3C);
    if (profilePtr)
    {
        uint32_t profileAddr = *(uint32_t *)profilePtr;
        if (profileAddr)
        {
            // Read mMapObjectId from MapObjectProfile (typically at offset 0x00, u32)
            void *mapObjIdPtr = GetRAM(profileAddr + 0x10);
            if (mapObjIdPtr)
            {
                data.mapObjectId = *(uint32_t *)mapObjIdPtr;
                data.mapObjectIdString = mapObjectIdToString(data.mapObjectId);
            }
        }
    }

    return data;
}

void MemWatchMapObject::refreshMapObjectList()
{
    // Get NDS first
    melonDS::NDS *nds = GetNDS();
    if (!nds)
    {
        clear();
        return;
    }

    // Clear killed mapobjects map since we're refreshing (they might be gone)
    m_killedMapObjects.clear();

    // The fixed address is a pointer to the MapObjectManager
    void *managerPtrPtr = GetRAM(MAPOBJECT_MANAGER_ADDRESS);
    if (!managerPtrPtr)
    {
        clear();
        return;
    }

    // Dereference to get the actual MapObjectManager address
    uint32_t managerAddress = *(uint32_t *)managerPtrPtr;
    if (!managerAddress)
    {
        clear();
        return;
    }

    // Read mMapObjTable and mMapObjTableEnd from MapObjectManager
    // mMapObjTable at offset 0x00 (pointer)
    // mMapObjTableEnd at offset 0x04 (pointer)
    void *tablePtr = GetRAM(managerAddress + 0x00);
    void *tableEndPtr = GetRAM(managerAddress + 0x04);

    if (!tablePtr || !tableEndPtr)
    {
        clear();
        return;
    }

    uint32_t mapobjectTableAddr = *(uint32_t *)tablePtr;
    uint32_t mapobjectTableEndAddr = *(uint32_t *)tableEndPtr;

    if (!mapobjectTableAddr || !mapobjectTableEndAddr)
    {
        clear();
        return;
    }

    // Read mapobject pointers from the table
    QMap<uint32_t, MapObjectData> newMapObjects;
    int mapobjectCount = 0;

    for (uint32_t tableOffset = 0; mapobjectCount < MAX_MAPOBJECTS; tableOffset += 4)
    {
        uint32_t currentAddr = mapobjectTableAddr + tableOffset;
        
        // Stop when we reach the end of the table
        if (currentAddr >= mapobjectTableEndAddr)
            break;

        void *mapobjectPtrPtr = GetRAM(currentAddr);
        if (!mapobjectPtrPtr)
            break;

        uint32_t mapobjectAddr = *(uint32_t *)mapobjectPtrPtr;
        if (!mapobjectAddr)
            continue;

        // Read mapobject data
        MapObjectData mapobjectData = readMapObjectData(mapobjectAddr);
        mapobjectData.tableEntryAddress = currentAddr;  // Store the table entry address
        newMapObjects[mapobjectAddr] = mapobjectData;
        mapobjectCount++;
    }

    // Update list widget with changed mapobjects
    m_cachedMapObjects = newMapObjects;

    clear();

    // Update parent dialog title with mapobject count
    QWidget *parentWidget = this->parentWidget();
    if (parentWidget)
    {
        parentWidget->setWindowTitle(QString("MapObject Explorer - %1 mapobjects").arg(newMapObjects.size()));
    }

    if (newMapObjects.isEmpty())
    {
        return;
    }

    // Convert to QList and sort by mapObjectIdString
    QList<MapObjectData> sortedMapObjects = newMapObjects.values();
    std::sort(sortedMapObjects.begin(), sortedMapObjects.end(), 
        [](const MapObjectData &a, const MapObjectData &b) {
            return a.mapObjectIdString < b.mapObjectIdString;
        });

    // Add mapobjects to list with formatted display
    for (const MapObjectData &mapobject : sortedMapObjects)
    {
        float posX = q20ToFloat(mapobject.posX);
        float posY = q20ToFloat(mapobject.posY);
        float posZ = q20ToFloat(mapobject.posZ);
        
        QString displayText = QString("[%1]  Pos:(%2,%3,%4)  F:0x%5")
            .arg(mapobject.mapObjectIdString, -4)
            .arg(posX, 7, 'f', 1)
            .arg(posY, 7, 'f', 1)
            .arg(posZ, 7, 'f', 1)
            .arg(mapobject.flags, 8, 16, QChar('0'));

        QListWidgetItem *item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, mapobject.tableEntryAddress);  // Store table entry address for kill/revive
        addItem(item);
    }
}

void MemWatchMapObject::updateMapObjects()
{
    refreshMapObjectList();
}
