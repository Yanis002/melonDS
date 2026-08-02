#include "MemWatchExplorerDialog.h"

#include <QListWidgetItem>
#include <QDebug>
#include <QMenu>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTabWidget>
#include <cstring>
#include <algorithm>

#include "types.h"
#include "NDS.h"
#include "Window.h"
#include "EmuInstance.h"

using namespace melonDS;

// ============ BASE CLASS IMPLEMENTATION ============

MemWatchExplorer::MemWatchExplorer(QWidget *parent)
    : QListWidget(parent)
    , m_updateTimer(nullptr)
    , m_manualMode(false)
{
    QFont monospaceFont("Courier", 9);
    setFont(monospaceFont);
    setMinimumHeight(200);
    
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &MemWatchExplorer::updateObjects);
    m_updateTimer->start(100);
    
    connect(this, &QListWidget::itemClicked, this, &MemWatchExplorer::onItemClicked);
    
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &MemWatchExplorer::onContextMenu);
}

MemWatchExplorer::~MemWatchExplorer()
{
    if (m_updateTimer)
    {
        m_updateTimer->stop();
        delete m_updateTimer;
    }
}

void MemWatchExplorer::setThrottleEnabled(bool enabled)
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

void MemWatchExplorer::setThrottleInterval(int milliseconds)
{
    if (m_updateTimer)
    {
        m_updateTimer->setInterval(milliseconds);
        if (m_updateTimer->isActive())
        {
            m_updateTimer->start();
        }
    }
}

void MemWatchExplorer::manualRefresh()
{
    updateObjects();
}

void MemWatchExplorer::setManualMode(bool manual)
{
    m_manualMode = manual;
    qDebug() << "Manual mode set to:" << manual;
}

void MemWatchExplorer::mousePressEvent(QMouseEvent *event)
{
    QListWidgetItem *item = itemAt(event->pos());
    
    if (event->button() == Qt::LeftButton && item)
    {
        if (m_manualMode)
        {
            int row = this->row(item);
            uint32_t objectAddress = item->data(Qt::UserRole).toUInt();
            bool isKilled = m_killedObjects.contains(row);
            
            qDebug() << "Left-click on row:" << row << "Address:" << QString::number(objectAddress, 16) << "Already killed:" << isKilled;
            
            toggleKill(row, !isKilled);
            
            qDebug() << "After toggle, killed map contains row:" << m_killedObjects.contains(row);
            
            if (m_killedObjects.contains(row))
            {
                item->setForeground(QColor(128, 128, 128));
                QString currentText = item->text();
                if (!currentText.endsWith(" [KILLED]"))
                    item->setText(currentText + " [KILLED]");
            }
            else
            {
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

void MemWatchExplorer::onItemClicked(QListWidgetItem *item)
{
}

void MemWatchExplorer::onContextMenu(const QPoint &pos)
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
        teleportLinkToObject(row);
    }
}

melonDS::NDS *MemWatchExplorer::GetNDS()
{
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

void *MemWatchExplorer::GetRAM(uint32_t address)
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

bool MemWatchExplorer::SetRAM(uint32_t address, const void *data, int size)
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

uint32_t MemWatchExplorer::GetEffectiveAddress(uint32_t baseAddress, uint32_t offsets[], bool *valid, bool isLastOffsetPointer)
{
  *valid = false;
  uint32_t address = baseAddress;

  if (!isLastOffsetPointer)
  {
    *valid = true;
    return address;
  }

  void *pBase = GetRAM(address);
  if (!pBase)
    return 0;

  uint32_t ptrValue = *(uint32_t *)pBase;

  int offsetCount = 0;
  while (offsets[offsetCount] != 0 || offsetCount == 0)
  {
    offsetCount++;
    if (offsetCount > 100) break;
  }

  for (int i = 0; i < offsetCount; i++)
  {
    int32_t offset = offsets[i];
    bool isLastOffset = (i == offsetCount - 1);

    address = ptrValue + offset;

    if (isLastOffset && !isLastOffsetPointer)
    {
      *valid = true;
      return address;
    }

    void *pNext = GetRAM(address);
    if (!pNext)
      return 0;
    ptrValue = *(uint32_t *)pNext;
  }

  if (isLastOffsetPointer)
  {
    address = ptrValue;
  }

  *valid = true;
  return address;
}

float MemWatchExplorer::q20ToFloat(int32_t q20Value)
{
    return ((q20Value * 2) - 1) / 8192.f;
}

void MemWatchExplorer::updateObjects()
{
    refreshList();
}

// ============ ACTOR IMPLEMENTATION ============

MemWatchActor::MemWatchActor(QWidget *parent)
    : MemWatchExplorer(parent)
{
    setWindowTitle("Actor Manager");
}

MemWatchActor::~MemWatchActor()
{
}

void MemWatchActor::toggleActorKill(int row, bool kill)
{
    toggleKill(row, kill);
}

void MemWatchActor::toggleKill(int row, bool kill)
{
    QListWidgetItem *item = this->item(row);
    if (!item) {
        qDebug() << "Item not found for row" << row;
        return;
    }

    uint32_t actorAddress = item->data(Qt::UserRole).toUInt();
    qDebug() << "toggleKill - row:" << row << "kill:" << kill << "actorAddress:" << QString::number(actorAddress, 16);
    
    if (!actorAddress) {
        qDebug() << "No actor address stored";
        return;
    }

    if (kill)
    {
        void *addrPtr = GetRAM(actorAddress);
        if (!addrPtr) {
            qDebug() << "Failed to get RAM at address" << QString::number(actorAddress, 16);
            return;
        }
        
        uint32_t originalAddress = *(uint32_t *)addrPtr;
        qDebug() << "Storing original address:" << QString::number(originalAddress, 16);
        m_killedObjects[row] = originalAddress;
        
        uint32_t zero = 0;
        bool success = SetRAM(actorAddress, &zero, 4);
        qDebug() << "SetRAM returned:" << success;
    }
    else if (m_killedObjects.contains(row))
    {
        uint32_t originalAddress = m_killedObjects[row];
        qDebug() << "Restoring address:" << QString::number(originalAddress, 16);
        bool success = SetRAM(actorAddress, &originalAddress, 4);
        qDebug() << "SetRAM restore returned:" << success;
        m_killedObjects.remove(row);
    }
}

void MemWatchActor::teleportLinkToActor(int row)
{
    teleportLinkToObject(row);
}

void MemWatchActor::teleportLinkToObject(int row)
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

QString MemWatchActor::actorIdToFourCC(uint32_t actorId)
{
    uint8_t bytes[4];
    bytes[3] = (actorId >> 0) & 0xFF;
    bytes[2] = (actorId >> 8) & 0xFF;
    bytes[1] = (actorId >> 16) & 0xFF;
    bytes[0] = (actorId >> 24) & 0xFF;
    
    QString result;
    for (int i = 0; i < 4; ++i)
    {
        char c = bytes[i];
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

QString MemWatchActor::getObjectIdString(uint32_t id)
{
    return actorIdToFourCC(id);
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
    data.params[0] = 0;
    data.params[1] = 0;
    data.params[2] = 0;
    data.params[3] = 0;

    if (!actorAddress)
        return data;

    void *posPtr = GetRAM(actorAddress + 0x04);
    if (posPtr)
    {
        int32_t *pos = (int32_t *)posPtr;
        data.posX = pos[0];
        data.posY = pos[1];
        data.posZ = pos[2];
    }

    void *velPtr = GetRAM(actorAddress + 0x1c);
    if (velPtr)
    {
        int32_t *vel = (int32_t *)velPtr;
        data.velX = vel[0];
        data.velY = vel[1];
        data.velZ = vel[2];
    }

    void *flagsPtr = GetRAM(actorAddress + 0x58);
    if (flagsPtr)
    {
        data.flags = *(uint32_t *)flagsPtr;
    }

    void *paramsPtr = GetRAM(actorAddress + 0x5C + 0x10);
    if (paramsPtr)
    {
        *(uint32_t*)data.params = *(uint32_t*)paramsPtr;
    }

    void *typePtr = GetRAM(actorAddress + 0x90);
    if (typePtr)
    {
        uint32_t actorTypeAddr = *(uint32_t *)typePtr;
        if (actorTypeAddr)
        {
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

void MemWatchActor::displayObject(const QString &displayText, uint32_t tableEntryAddress)
{
    QListWidgetItem *item = new QListWidgetItem(displayText);
    item->setData(Qt::UserRole, tableEntryAddress);
    addItem(item);
}

void MemWatchActor::refreshList()
{
    melonDS::NDS *nds = GetNDS();
    if (!nds)
    {
        clear();
        return;
    }

    m_killedObjects.clear();

    void *managerPtrPtr = GetRAM(ACTOR_MANAGER_ADDRESS);
    if (!managerPtrPtr)
    {
        clear();
        return;
    }

    uint32_t managerAddress = *(uint32_t *)managerPtrPtr;
    if (!managerAddress)
    {
        clear();
        return;
    }

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

    QMap<uint32_t, ActorData> newActors;
    int actorCount = 0;

    for (uint32_t tableOffset = 0; actorCount < MAX_ACTORS; tableOffset += 4)
    {
        uint32_t currentAddr = actorTableAddr + tableOffset;
        
        if (currentAddr >= actorTableEndAddr)
            break;

        void *actorPtrPtr = GetRAM(currentAddr);
        if (!actorPtrPtr)
            break;

        uint32_t actorAddr = *(uint32_t *)actorPtrPtr;
        if (!actorAddr)
            continue;

        ActorData actorData = readActorData(actorAddr);
        actorData.tableEntryAddress = currentAddr;
        newActors[actorAddr] = actorData;
        actorCount++;
    }

    m_cachedActors = newActors;

    clear();

    QWidget *parentWidget = this->parentWidget();
    if (parentWidget)
    {
        parentWidget->setWindowTitle(QString("Actor Explorer - %1 actors").arg(newActors.size()));
    }

    if (newActors.isEmpty())
    {
        return;
    }

    QList<ActorData> sortedActors = newActors.values();
    std::sort(sortedActors.begin(), sortedActors.end(), 
        [](const ActorData &a, const ActorData &b) {
            return a.actorIdString < b.actorIdString;
        });

    for (const ActorData &actor : sortedActors)
    {
        float posX = q20ToFloat(actor.posX);
        float posY = q20ToFloat(actor.posY);
        float posZ = q20ToFloat(actor.posZ);
        float velX = q20ToFloat(actor.velX);
        float velY = q20ToFloat(actor.velY);
        float velZ = q20ToFloat(actor.velZ);

        QString displayText = QString("[%1]  Pos:(%2,%3,%4)  Vel:(%5,%6,%7)  F:0x%8  P:0x%9;0x%10;0x%11;0x%12 @ 0x%13")
            .arg(actor.actorIdString, -4)
            .arg(posX, 7, 'f', 1)
            .arg(posY, 7, 'f', 1)
            .arg(posZ, 7, 'f', 1)
            .arg(velX, 7, 'f', 1)
            .arg(velY, 7, 'f', 1)
            .arg(velZ, 7, 'f', 1)
            .arg(actor.flags, 8, 16, QChar('0'))
            .arg(actor.params[0], 4, 16, QChar('0'))
            .arg(actor.params[1], 4, 16, QChar('0'))
            .arg(actor.params[2], 4, 16, QChar('0'))
            .arg(actor.params[3], 4, 16, QChar('0'))
            .arg(actor.address, 8, 16, QChar('0'));

        displayObject(displayText, actor.tableEntryAddress);
    }
}

// ============ MAPOBJECT IMPLEMENTATION ============

MemWatchMapObject::MemWatchMapObject(QWidget *parent)
    : MemWatchExplorer(parent)
{
    setWindowTitle("MapObject Manager");
}

MemWatchMapObject::~MemWatchMapObject()
{
}

void MemWatchMapObject::toggleMapObjectKill(int row, bool kill)
{
    toggleKill(row, kill);
}

void MemWatchMapObject::toggleKill(int row, bool kill)
{
    QListWidgetItem *item = this->item(row);
    if (!item) {
        qDebug() << "Item not found for row" << row;
        return;
    }

    uint32_t mapobjectAddress = item->data(Qt::UserRole).toUInt();
    qDebug() << "toggleKill - row:" << row << "kill:" << kill << "mapobjectAddress:" << QString::number(mapobjectAddress, 16);
    
    if (!mapobjectAddress) {
        qDebug() << "No mapobject address stored";
        return;
    }

    if (kill)
    {
        void *addrPtr = GetRAM(mapobjectAddress);
        if (!addrPtr) {
            qDebug() << "Failed to get RAM at address" << QString::number(mapobjectAddress, 16);
            return;
        }
        
        uint32_t originalAddress = *(uint32_t *)addrPtr;
        qDebug() << "Storing original address:" << QString::number(originalAddress, 16);
        m_killedObjects[row] = originalAddress;
        
        uint32_t zero = 0;
        bool success = SetRAM(mapobjectAddress, &zero, 4);
        qDebug() << "SetRAM returned:" << success;
    }
    else if (m_killedObjects.contains(row))
    {
        uint32_t originalAddress = m_killedObjects[row];
        qDebug() << "Restoring address:" << QString::number(originalAddress, 16);
        bool success = SetRAM(mapobjectAddress, &originalAddress, 4);
        qDebug() << "SetRAM restore returned:" << success;
        m_killedObjects.remove(row);
    }
}

void MemWatchMapObject::teleportLinkToMapObject(int row)
{
    teleportLinkToObject(row);
}

void MemWatchMapObject::teleportLinkToObject(int row)
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

QString MemWatchMapObject::mapObjectIdToString(uint32_t mapObjectId)
{
    uint8_t bytes[4];
    bytes[3] = (mapObjectId >> 0) & 0xFF;
    bytes[2] = (mapObjectId >> 8) & 0xFF;
    bytes[1] = (mapObjectId >> 16) & 0xFF;
    bytes[0] = (mapObjectId >> 24) & 0xFF;
    
    QString result;
    for (int i = 0; i < 4; ++i)
    {
        char c = bytes[i];
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

QString MemWatchMapObject::getObjectIdString(uint32_t id)
{
    return mapObjectIdToString(id);
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
    data.params[0] = 0;
    data.params[1] = 0;
    data.params[2] = 0;
    data.params[3] = 0;
    data.initialPos[0] = 0;
    data.initialPos[1] = 0;

    if (!mapobjectAddress)
        return data;

    void *posPtr = GetRAM(mapobjectAddress + 0x04);
    if (posPtr)
    {
        int32_t *pos = (int32_t *)posPtr;
        data.posX = pos[0];
        data.posY = pos[1];
        data.posZ = pos[2];
    }

    void *flagsPtr = GetRAM(mapobjectAddress + 0x1C);
    if (flagsPtr)
    {
        data.flags = *(unsigned short *)flagsPtr;
    }

    void *paramsPtr = GetRAM(mapobjectAddress + 0x20 + 0x00);
    if (paramsPtr)
    {
        *(uint32_t*)data.params = *(uint32_t*)paramsPtr;
    }

    void *initialposPtr = GetRAM(mapobjectAddress + 0x3A);
    if (initialposPtr)
    {
        *(unsigned short*)data.initialPos = *(unsigned short*)initialposPtr;
    }

    void *profilePtr = GetRAM(mapobjectAddress + 0x3C);
    if (profilePtr)
    {
        uint32_t profileAddr = *(uint32_t *)profilePtr;
        if (profileAddr)
        {
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

void MemWatchMapObject::displayObject(const QString &displayText, uint32_t tableEntryAddress)
{
    QListWidgetItem *item = new QListWidgetItem(displayText);
    item->setData(Qt::UserRole, tableEntryAddress);
    addItem(item);
}

void MemWatchMapObject::refreshList()
{
    melonDS::NDS *nds = GetNDS();
    if (!nds)
    {
        clear();
        return;
    }

    m_killedObjects.clear();

    void *managerPtrPtr = GetRAM(MAPOBJECT_MANAGER_ADDRESS);
    if (!managerPtrPtr)
    {
        clear();
        return;
    }

    uint32_t managerAddress = *(uint32_t *)managerPtrPtr;
    if (!managerAddress)
    {
        clear();
        return;
    }

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

    QMap<uint32_t, MapObjectData> newMapObjects;
    int mapobjectCount = 0;

    for (uint32_t tableOffset = 0; mapobjectCount < MAX_MAPOBJECTS; tableOffset += 4)
    {
        uint32_t currentAddr = mapobjectTableAddr + tableOffset;
        
        if (currentAddr >= mapobjectTableEndAddr)
            break;

        void *mapobjectPtrPtr = GetRAM(currentAddr);
        if (!mapobjectPtrPtr)
            break;

        uint32_t mapobjectAddr = *(uint32_t *)mapobjectPtrPtr;
        if (!mapobjectAddr)
            continue;

        MapObjectData mapobjectData = readMapObjectData(mapobjectAddr);
        mapobjectData.tableEntryAddress = currentAddr;
        newMapObjects[mapobjectAddr] = mapobjectData;
        mapobjectCount++;
    }

    m_cachedMapObjects = newMapObjects;

    clear();

    QWidget *parentWidget = this->parentWidget();
    if (parentWidget)
    {
        parentWidget->setWindowTitle(QString("MapObject Explorer - %1 mapobjects").arg(newMapObjects.size()));
    }

    if (newMapObjects.isEmpty())
    {
        return;
    }

    QList<MapObjectData> sortedMapObjects = newMapObjects.values();
    std::sort(sortedMapObjects.begin(), sortedMapObjects.end(), 
        [](const MapObjectData &a, const MapObjectData &b) {
            return a.mapObjectIdString < b.mapObjectIdString;
        });

    for (const MapObjectData &mapobject : sortedMapObjects)
    {
        float posX = q20ToFloat(mapobject.posX);
        float posY = q20ToFloat(mapobject.posY);
        float posZ = q20ToFloat(mapobject.posZ);
        
        QString displayText = QString("[%1_0x%2_0x%3]  Pos:(%4,%5,%6)  F:0x%7  P:0x%8;0x%9;0x%10;0x%11 @ 0x%12")
            .arg(mapobject.mapObjectIdString, -4)
            .arg(mapobject.initialPos[0], 2, 16, QChar('0'))
            .arg(mapobject.initialPos[1], 2, 16, QChar('0'))
            .arg(posX, 7, 'f', 1)
            .arg(posY, 7, 'f', 1)
            .arg(posZ, 7, 'f', 1)
            .arg(mapobject.flags, 4, 16, QChar('0'))
            .arg(mapobject.params[0], 4, 16, QChar('0'))
            .arg(mapobject.params[1], 4, 16, QChar('0'))
            .arg(mapobject.params[2], 4, 16, QChar('0'))
            .arg(mapobject.params[3], 4, 16, QChar('0'))
            .arg(mapobject.address, 8, 16, QChar('0'));

        displayObject(displayText, mapobject.tableEntryAddress);
    }
}

// ============ DIALOG IMPLEMENTATION ============

MemWatchActorDialog* MemWatchActorDialog::currentDlg = nullptr;

MemWatchActorDialog::MemWatchActorDialog(QWidget* parent)
    : QDialog(parent)
    , m_tabWidget(nullptr)
    , m_actorWidget(nullptr)
    , m_mapObjectWidget(nullptr)
    , m_throttledCheckbox(nullptr)
    , m_throttleComboBox(nullptr)
    , m_searchButton(nullptr)
{
    setWindowTitle("Explorer");
    setGeometry(100, 100, 900, 600);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);
    
    m_tabWidget = new QTabWidget();
    
    m_actorWidget = new MemWatchActor(this);
    m_tabWidget->addTab(m_actorWidget, "Actor Explorer");
    
    m_mapObjectWidget = new MemWatchMapObject(this);
    m_tabWidget->addTab(m_mapObjectWidget, "MapObject Explorer");
    
    connect(m_tabWidget, QOverload<int>::of(&QTabWidget::currentChanged), this, &MemWatchActorDialog::onTabChanged);
    
    mainLayout->addWidget(m_tabWidget);
    
    QHBoxLayout* controlLayout = new QHBoxLayout();
    
    m_throttledCheckbox = new QCheckBox("Throttled Search");
    m_throttledCheckbox->setChecked(true);
    controlLayout->addWidget(m_throttledCheckbox);
    
    controlLayout->addWidget(new QLabel("Interval (ms):"));
    
    m_throttleComboBox = new QComboBox();
    m_throttleComboBox->addItem("100", 100);
    m_throttleComboBox->addItem("250", 250);
    m_throttleComboBox->addItem("500", 500);
    m_throttleComboBox->addItem("1000", 1000);
    m_throttleComboBox->setCurrentIndex(1);
    controlLayout->addWidget(m_throttleComboBox);
    
    m_searchButton = new QPushButton("Search Now");
    m_searchButton->setEnabled(false);
    controlLayout->addWidget(m_searchButton);
    
    controlLayout->addStretch();
    
    mainLayout->addLayout(controlLayout);
    
    setLayout(mainLayout);
    
    connect(m_throttledCheckbox, &QCheckBox::toggled, this, &MemWatchActorDialog::onThrottledToggled);
    connect(m_throttleComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MemWatchActorDialog::onThrottleValueChanged);
    connect(m_searchButton, &QPushButton::clicked, this, &MemWatchActorDialog::onManualSearch);
}

MemWatchActorDialog::~MemWatchActorDialog()
{
    currentDlg = nullptr;
}

void MemWatchActorDialog::closeEvent(QCloseEvent* event)
{
    currentDlg = nullptr;
    QDialog::closeEvent(event);
}

void MemWatchActorDialog::onThrottledToggled(bool checked)
{
    m_throttleComboBox->setEnabled(checked);
    m_searchButton->setEnabled(!checked);
    m_actorWidget->setThrottleEnabled(checked);
    m_actorWidget->setManualMode(!checked);
    m_mapObjectWidget->setThrottleEnabled(checked);
    m_mapObjectWidget->setManualMode(!checked);
}

void MemWatchActorDialog::onThrottleValueChanged(int index)
{
    int interval = m_throttleComboBox->itemData(index).toInt();
    m_actorWidget->setThrottleInterval(interval);
    m_mapObjectWidget->setThrottleInterval(interval);
}

void MemWatchActorDialog::onManualSearch()
{
    if (m_tabWidget->currentIndex() == 0)
    {
        m_actorWidget->manualRefresh();
    }
    else
    {
        m_mapObjectWidget->manualRefresh();
    }
}

void MemWatchActorDialog::onTabChanged(int index)
{
}
