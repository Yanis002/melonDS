#include "MemWatchActorDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>

MemWatchActorDialog* MemWatchActorDialog::currentDlg = nullptr;

MemWatchActorDialog::MemWatchActorDialog(QWidget* parent)
    : QDialog(parent)
    , m_actorWidget(nullptr)
    , m_throttledCheckbox(nullptr)
    , m_throttleComboBox(nullptr)
    , m_searchButton(nullptr)
{
    setWindowTitle("Actor Explorer");
    setGeometry(100, 100, 900, 600);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);
    
    // Control panel
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
    m_throttleComboBox->setCurrentIndex(1); // Default to 250ms
    controlLayout->addWidget(m_throttleComboBox);
    
    m_searchButton = new QPushButton("Search Now");
    m_searchButton->setEnabled(false); // Disabled when throttled is ON
    controlLayout->addWidget(m_searchButton);
    
    controlLayout->addStretch();
    
    mainLayout->addLayout(controlLayout);
    
    // Actor list widget
    m_actorWidget = new MemWatchActor(this);
    mainLayout->addWidget(m_actorWidget);
    
    setLayout(mainLayout);
    
    // Connect signals
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
    m_actorWidget->setManualMode(!checked);  // Manual mode when throttle is OFF
}

void MemWatchActorDialog::onThrottleValueChanged(int index)
{
    int interval = m_throttleComboBox->itemData(index).toInt();
    m_actorWidget->setThrottleInterval(interval);
}

void MemWatchActorDialog::onManualSearch()
{
    m_actorWidget->manualRefresh();
}
