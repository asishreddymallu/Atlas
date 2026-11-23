#include "cache_config_widget.h"
#include "ui_cache_config_widget.h"
#include <QFileDialog>
#include <QMessageBox>


namespace Atlas
{

CacheConfigWidget::CacheConfigWidget(QWidget *parent)
    : QWidget(parent), ui(new Ui::CacheConfigWidget)
{
    ui->setupUi(this);
    ui->sizeLineEdit->setReadOnly(true);
    ui->hitsLineEdit->setReadOnly(true);
    ui->missesLineEdit->setReadOnly(true);
    ui->writeBackLineEdit->setReadOnly(true);
    ui->hitrateLineEdit->setReadOnly(true);
    ui->evictionsLineEdit->setReadOnly(true);
    ui->evictionsLabel->setToolTip(ui->evictionsLineEdit->toolTip());

    ui->customPolicyScriptlineEdit->setReadOnly(true);

    // Each choice carries its engine value, so what is shown and what is
    // sent never depend on the order of the items in the .ui. (Write hit
    // lists Write Back first, while WritePolicy::WriteThrough is 0, and no
    // item had data: getConfig() sent 0 - write-through, write-allocate,
    // LRU - whatever was shown.)
    auto tag = [](QComboBox *box, const char *text, int value)
    {
        const int i = box->findText(QString::fromLatin1(text));
        if (i >= 0)
            box->setItemData(i, value);
    };
    tag(ui->writeHitComboBox, "Write Through", static_cast<int>(WritePolicy::WriteThrough));
    tag(ui->writeHitComboBox, "Write Back", static_cast<int>(WritePolicy::WriteBack));
    tag(ui->writeMissComboBox, "Write Allocate", static_cast<int>(AllocationPolicy::WriteAllocate));
    tag(ui->writeMissComboBox, "Write No Allocate", static_cast<int>(AllocationPolicy::NoWriteAllocate));
    tag(ui->repPolComboBox, "LRU", static_cast<int>(ReplacementPolicy::LRU));
    tag(ui->repPolComboBox, "FIFO", static_cast<int>(ReplacementPolicy::FIFO));
    tag(ui->repPolComboBox, "Custom", static_cast<int>(ReplacementPolicy::Custom));

    connect(ui->linesSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &CacheConfigWidget::configChangedSignal);
    connect(ui->waysSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &CacheConfigWidget::configChangedSignal);
    connect(ui->wordsSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &CacheConfigWidget::configChangedSignal);

    connect(ui->writeHitComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &CacheConfigWidget::configChangedSignal);
    connect(ui->writeMissComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &CacheConfigWidget::configChangedSignal);
    connect(ui->repPolComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
    [this]
    {
        if (static_cast<ReplacementPolicy>(ui->repPolComboBox->currentData().toInt()) ==
            ReplacementPolicy::Custom)
        {
            onCustomPolicyClicked();
        }
        else
        {
            ui->customPolicyScriptlineEdit->clear();
            m_lastSelectedPolicy =
                static_cast<ReplacementPolicy>(ui->repPolComboBox->currentData().toInt());
            emit CacheConfigWidget::configChangedSignal();
        }
    });
}

void CacheConfigWidget::onCustomPolicyClicked()
{
    QString fileName = QFileDialog::getOpenFileName(
        this, tr("Select Custom Replacement Policy Script"), "", tr("lua Scripts (*.lua)"));
    if (!fileName.isEmpty())
    {
        // ui->customPolicyScriptlineEdit->setText(fileName);
        emit customPolicyScriptSelectedSignal(fileName.toStdString());
    }
    else
    {
        // If no file was selected, reset the combo box to a default value (e.g., LRU)
        QSignalBlocker blocker(ui->repPolComboBox);
        ui->repPolComboBox->setCurrentIndex(
            ui->repPolComboBox->findData(static_cast<int>(ReplacementPolicy::LRU)));
        ui->customPolicyScriptlineEdit->clear();
        QMessageBox::warning(this, tr("No Script Selected"),
                             tr("No custom policy script was selected. Reverting to LRU."));
    }
}
//

CacheConfig CacheConfigWidget::getConfig() const
{
    CacheConfig config;
    config.lineCount = 1ULL << ui->linesSpinBox->value();
    config.lineSizeInBytes = (1ULL <<ui->wordsSpinBox->value())* sizeof(uint32_t);
    config.wayCount = 1ULL << ui->waysSpinBox->value();
    config.writePolicy = static_cast<WritePolicy>(ui->writeHitComboBox->currentData().toInt());
    config.allocationPolicy = static_cast<AllocationPolicy>(ui->writeMissComboBox->currentData().toInt());
    config.replacementPolicy = static_cast<ReplacementPolicy>(ui->repPolComboBox->currentData().toInt());
    return config;
}

int CacheConfigWidget::getLinesExponent() const
{
    return ui->linesSpinBox->value();
}

int CacheConfigWidget::getWaysExponent() const
{
    return ui->waysSpinBox->value();
}

int CacheConfigWidget::getWordsExponent() const
{
    return ui->wordsSpinBox->value();
}

void CacheConfigWidget::setLinesExponent(int value)
{
    ui->linesSpinBox->setValue(value);
}

void CacheConfigWidget::setWaysExponent(int value)
{
    ui->waysSpinBox->setValue(value);
}

void CacheConfigWidget::setWordsExponent(int value)
{
    ui->wordsSpinBox->setValue(value);
}

void CacheConfigWidget::setConfig(CacheConfig config)
{
    QSignalBlocker blocker(this);
    ui->linesSpinBox->setValue(static_cast<int>(std::log2(config.lineCount)));
    ui->waysSpinBox->setValue(static_cast<int>(std::log2(config.wayCount)));
    ui->wordsSpinBox->setValue(static_cast<int>(std::log2(config.lineSizeInBytes/sizeof(uint32_t))));
    ui->writeHitComboBox->setCurrentIndex(ui->writeHitComboBox->findData(static_cast<int>(config.writePolicy)));
    ui->writeMissComboBox->setCurrentIndex(
        ui->writeMissComboBox->findData(static_cast<int>(config.allocationPolicy)));
    ui->repPolComboBox->setCurrentIndex(ui->repPolComboBox->findData(static_cast<int>(config.replacementPolicy)));
    if (config.replacementPolicy != ReplacementPolicy::Custom)
        m_lastSelectedPolicy = config.replacementPolicy;
}

std::string CacheConfigWidget::customPolicyScriptPath() const
{
    return ui->customPolicyScriptlineEdit->text().toStdString();
}

void CacheConfigWidget::cacheStatsUpdatedSlot(CacheStats newStats)
{
    ui->sizeLineEdit->setText(QString::number(newStats.cacheSizeInBytes) + " bytes");
    ui->hitsLineEdit->setText(QString::number(newStats.hitCount));
    ui->missesLineEdit->setText(QString::number(newStats.missCount));
    ui->writeBackLineEdit->setText(QString::number(newStats.writeBackCount));
    ui->evictionsLineEdit->setText(QString::number(newStats.evictionCount));
    // CacheStats::hitRate is a fraction (0..1, Cache::updateStats); shown as
    // a percentage.
    ui->hitrateLineEdit->setText(QString::number(newStats.hitRate * 100.0, 'f', 2) + " %");
}

void CacheConfigWidget::customPolicyScriptLoadedSlot(bool success, const std::string &message)
{
    QString messageStr = QString::fromStdString(message);
    if (success)
    {
        ui->customPolicyScriptlineEdit->setText(
            messageStr); // message contains the script path on success
    }
    else
    {
        ui->customPolicyScriptlineEdit->clear();
        QMessageBox::critical(this, tr("Custom Policy Load Failed"),
                              tr("Failed to load custom policy script: %1").arg(messageStr));
        QSignalBlocker blocker(ui->repPolComboBox);
        ui->repPolComboBox->setCurrentIndex(
            static_cast<int>(m_lastSelectedPolicy)); // revert to last selected policy on failure
    }
}

// void CacheConfigWidget::UpdateSize()
// {
//     const size_t size = (size_t(1) << ui->linesSpinBox->value()) *
//                         (size_t(1) << ui->wordsSpinBox->value()) *
//                         (size_t(1) << ui->waysSpinBox->value());
//     ui->sizeLineEdit->setText(QString::number(size) + " bytes");
// }

CacheConfigWidget::~CacheConfigWidget()
{
    delete ui;
}
} // namespace Atlas
