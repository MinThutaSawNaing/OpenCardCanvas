#include "ui/PersonalizationPanel.h"

#include "core/CardDocument.h"
#include "personalization/BatchRenderer.h"
#include "project/AssetStore.h"
#include "ui/IconFactory.h"
#include "utils/Settings.h"

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace occ {

namespace {

constexpr int kPreviewDpi = 300;
constexpr int kCheckedColumn = 0;
constexpr int kFirstDataColumn = 1;

// How many rows are shown in the record table. A hundred is enough to check the
// mapping and small enough that opening a fifty thousand row export is instant.
constexpr int kPreviewRowLimit = 100;

} // namespace

PersonalizationPanel::PersonalizationPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("PersonalizationPanel"));
    m_photoStore = new AssetStore();
    buildUi();
}

PersonalizationPanel::~PersonalizationPanel()
{
    delete m_photoStore;
    m_photoStore = nullptr;
}

void PersonalizationPanel::setDocument(CardDocument *document)
{
    m_document = document;
    rebuildMappingTable();
    updateWarnings();
    updatePreview();
}

void PersonalizationPanel::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // --- data set -----------------------------------------------------------
    auto *loadRow = new QHBoxLayout();
    auto *loadButton = new QPushButton(IconFactory::icon(QStringLiteral("data")),
                                       tr("Load data file..."), this);
    loadButton->setToolTip(tr("A CSV export: one row per card, one column per field."));
    m_fileLabel = new QLabel(tr("No data set loaded."), this);
    m_fileLabel->setWordWrap(true);
    loadRow->addWidget(loadButton);
    loadRow->addWidget(m_fileLabel, 1);
    layout->addLayout(loadRow);
    connect(loadButton, &QPushButton::clicked, this,
            &PersonalizationPanel::loadDataFileInteractive);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setObjectName(QStringLiteral("PersonalizationError"));
    m_errorLabel->setVisible(false);
    layout->addWidget(m_errorLabel);

    auto *splitter = new QSplitter(Qt::Vertical, this);

    // --- records ------------------------------------------------------------
    auto *recordsGroup = new QGroupBox(tr("Records"), splitter);
    auto *recordsLayout = new QVBoxLayout(recordsGroup);

    auto *selectRow = new QHBoxLayout();
    auto *selectAll = new QPushButton(tr("Select all"), recordsGroup);
    auto *selectNone = new QPushButton(tr("Select none"), recordsGroup);
    auto *invert = new QPushButton(tr("Invert"), recordsGroup);
    selectRow->addWidget(selectAll);
    selectRow->addWidget(selectNone);
    selectRow->addWidget(invert);
    selectRow->addStretch(1);
    selectRow->addWidget(new QLabel(tr("Preview record"), recordsGroup));
    m_previewCount = new QSpinBox(recordsGroup);
    m_previewCount->setRange(1, kPreviewRowLimit);
    m_previewCount->setToolTip(tr("How many rows of the data set are listed here."));
    m_previewCount->setValue(kPreviewRowLimit);
    selectRow->addWidget(m_previewCount);
    recordsLayout->addLayout(selectRow);

    m_records = new QTableWidget(recordsGroup);
    m_records->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_records->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_records->setSelectionMode(QAbstractItemView::SingleSelection);
    m_records->setAlternatingRowColors(true);
    m_records->verticalHeader()->setVisible(false);
    recordsLayout->addWidget(m_records);
    splitter->addWidget(recordsGroup);

    connect(selectAll, &QPushButton::clicked, this, [this] { setAllChecked(true); });
    connect(selectNone, &QPushButton::clicked, this, [this] { setAllChecked(false); });
    connect(invert, &QPushButton::clicked, this, &PersonalizationPanel::invertChecked);
    connect(m_records, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) {
                if (row >= 0 && row != m_previewRow) {
                    m_previewRow = row;
                    updatePreview();
                }
            });
    connect(m_records, &QTableWidget::itemChanged, this, [this] {
        if (m_updating)
            return;
        m_printButton->setText(tr("Print selected records (%1)")
                                   .arg(selectedRecords().size()));
    });
    connect(m_previewCount, &QSpinBox::valueChanged, this, [this] { rebuildRecordTable(); });

    // --- mapping ------------------------------------------------------------
    auto *mappingGroup = new QGroupBox(tr("Field mapping"), splitter);
    auto *mappingLayout = new QVBoxLayout(mappingGroup);

    auto *mappingRow = new QHBoxLayout();
    m_mappingTable = new QTableWidget(mappingGroup);
    m_mappingTable->setColumnCount(2);
    m_mappingTable->setHorizontalHeaderLabels(QStringList{ tr("Card field"), tr("Data column") });
    m_mappingTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_mappingTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_mappingTable->verticalHeader()->setVisible(false);
    m_mappingTable->setMinimumHeight(110);
    mappingRow->addWidget(m_mappingTable, 3);

    auto *mappingButtons = new QVBoxLayout();
    auto *photoLabel = new QLabel(tr("Photo column"), mappingGroup);
    m_photoColumn = new QComboBox(mappingGroup);
    m_photoColumn->setToolTip(tr("The column that holds the file name of each card's "
                                 "photograph, relative to the CSV file."));
    auto *saveMappingButton = new QPushButton(tr("Save mapping..."), mappingGroup);
    auto *loadMappingButton = new QPushButton(tr("Load mapping..."), mappingGroup);
    mappingButtons->addWidget(photoLabel);
    mappingButtons->addWidget(m_photoColumn);
    mappingButtons->addWidget(saveMappingButton);
    mappingButtons->addWidget(loadMappingButton);
    mappingButtons->addStretch(1);
    mappingRow->addLayout(mappingButtons, 2);
    mappingLayout->addLayout(mappingRow);
    splitter->addWidget(mappingGroup);

    connect(saveMappingButton, &QPushButton::clicked, this, &PersonalizationPanel::saveMapping);
    connect(loadMappingButton, &QPushButton::clicked, this, &PersonalizationPanel::loadMapping);
    connect(m_photoColumn, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_updating)
            return;
        m_mapping.photoAssetPlaceholder =
            m_photoColumn->currentIndex() <= 0 ? QString()
                                               : m_photoColumn->currentData().toString();
        if (!m_table.sourcePath.isEmpty())
            m_mapping.relativePhotoBaseDir = QFileInfo(m_table.sourcePath).absolutePath();
        updateWarnings();
        updatePreview();
    });
    connect(m_mappingTable, &QTableWidget::itemChanged, this, [this] {
        if (m_updating)
            return;
        collectMappingFromTable();
        updateWarnings();
        updatePreview();
    });

    // --- preview ------------------------------------------------------------
    auto *previewGroup = new QGroupBox(tr("Card preview"), splitter);
    auto *previewLayout = new QVBoxLayout(previewGroup);
    m_preview = new QLabel(previewGroup);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(140);
    m_preview->setText(tr("Load a data set to preview a personalised card."));
    m_previewInfo = new QLabel(previewGroup);
    m_previewInfo->setWordWrap(true);
    previewLayout->addWidget(m_preview);
    previewLayout->addWidget(m_previewInfo);
    splitter->addWidget(previewGroup);
    layout->addWidget(splitter, 1);

    m_warningLabel = new QLabel(this);
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setObjectName(QStringLiteral("PersonalizationWarnings"));
    layout->addWidget(m_warningLabel);

    auto *printRow = new QHBoxLayout();
    printRow->addStretch(1);
    m_printButton = new QPushButton(IconFactory::icon(QStringLiteral("print")),
                                    tr("Print selected records (0)"), this);
    m_printButton->setToolTip(tr("Prints one card per selected record through the printer "
                                 "chosen in Printer settings."));
    m_printButton->setEnabled(false);
    printRow->addWidget(m_printButton);
    layout->addLayout(printRow);
    connect(m_printButton, &QPushButton::clicked, this, [this] {
        const QVector<int> records = selectedRecords();
        if (records.isEmpty())
            return;
        emit statusMessage(tr("Printing %1 records...").arg(records.size()));
        emit printRequested(records);
    });
}

void PersonalizationPanel::reportError(const QString &error)
{
    m_errorLabel->setText(error);
    m_errorLabel->setVisible(!error.isEmpty());
    if (!error.isEmpty())
        emit statusMessage(error);
}

void PersonalizationPanel::loadDataFileInteractive()
{
    const QString start = AppSettings::instance().lastOpenDirectory();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load data file"), start,
        tr("Data files (*.csv *.txt *.tsv);;All files (*)"));
    if (path.isEmpty())
        return;
    AppSettings::instance().setLastOpenDirectory(QFileInfo(path).absolutePath());
    loadDataFile(path);
}

bool PersonalizationPanel::loadDataFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        reportError(tr("The file could not be opened: %1").arg(file.errorString()));
        return false;
    }

    // A large export is read with a progress dialog that can actually be
    // cancelled, rather than freezing the window while it parses.
    const qint64 size = file.size();
    QProgressDialog progress(tr("Reading %1...").arg(QFileInfo(path).fileName()),
                             tr("Cancel"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(400);
    if (size > 2 * 1024 * 1024)
        progress.setValue(5);

    QString error;
    CsvImporter::Options options;
    m_table = CsvImporter::loadFile(path, options, &error);
    progress.setValue(100);

    if (!error.isEmpty() || m_table.headers.isEmpty()) {
        reportError(error.isEmpty() ? tr("The file does not contain any columns.") : error);
        m_table = CsvTable();
        rebuildRecordTable();
        rebuildMappingTable();
        updateWarnings();
        updatePreview();
        return false;
    }

    reportError(QString());
    m_mapping = DataMapper::suggestMapping(m_table, m_document ? m_document->placeholders()
                                                              : QStringList());
    if (!m_table.sourcePath.isEmpty())
        m_mapping.relativePhotoBaseDir = QFileInfo(m_table.sourcePath).absolutePath();

    rebuildRecordTable();
    rebuildMappingTable();
    updateWarnings();
    updatePreview();

    m_fileLabel->setText(tr("%1 - %2 records, %3 columns")
                             .arg(QFileInfo(path).fileName())
                             .arg(m_table.rowCount())
                             .arg(m_table.columnCount()));
    emit statusMessage(tr("Loaded %1 records from \"%2\".")
                           .arg(m_table.rowCount())
                           .arg(QFileInfo(path).fileName()));
    return true;
}

void PersonalizationPanel::rebuildRecordTable()
{
    m_updating = true;
    m_records->clear();
    m_records->setColumnCount(m_table.columnCount() + 1);
    QStringList headers;
    headers << tr("Print");
    headers += m_table.headers;
    m_records->setHorizontalHeaderLabels(headers);
    m_records->horizontalHeader()->setSectionResizeMode(kCheckedColumn, QHeaderView::ResizeToContents);
    for (int column = kFirstDataColumn; column < m_records->columnCount(); ++column)
        m_records->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);

    const int limit = m_previewCount ? m_previewCount->value() : kPreviewRowLimit;
    const int rows = qMin(limit, m_table.rowCount());
    m_records->setRowCount(rows);
    for (int row = 0; row < rows; ++row) {
        auto *check = new QTableWidgetItem();
        check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        check->setCheckState(Qt::Checked);
        m_records->setItem(row, kCheckedColumn, check);
        for (int column = 0; column < m_table.columnCount(); ++column) {
            m_records->setItem(row, column + kFirstDataColumn,
                               new QTableWidgetItem(m_table.value(row, column)));
        }
    }
    m_updating = false;

    m_printButton->setEnabled(!m_table.isEmpty());
    m_printButton->setText(tr("Print selected records (%1)").arg(selectedRecords().size()));

    if (m_table.rowCount() > rows) {
        emit statusMessage(tr("The data set holds %1 records; the first %2 are listed, and every "
                              "record can still be printed.")
                               .arg(m_table.rowCount())
                               .arg(rows));
    }
}

QVector<int> PersonalizationPanel::selectedRecords() const
{
    QVector<int> records;
    if (!m_records)
        return records;
    for (int row = 0; row < m_records->rowCount(); ++row) {
        if (QTableWidgetItem *item = m_records->item(row, kCheckedColumn)) {
            if (item->checkState() == Qt::Checked)
                records.append(row);
        }
    }
    return records;
}

void PersonalizationPanel::setAllChecked(bool checked)
{
    m_updating = true;
    for (int row = 0; row < m_records->rowCount(); ++row) {
        if (QTableWidgetItem *item = m_records->item(row, kCheckedColumn))
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
    m_updating = false;
    m_printButton->setText(tr("Print selected records (%1)").arg(selectedRecords().size()));
}

void PersonalizationPanel::invertChecked()
{
    m_updating = true;
    for (int row = 0; row < m_records->rowCount(); ++row) {
        if (QTableWidgetItem *item = m_records->item(row, kCheckedColumn)) {
            item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked
                                                                  : Qt::Checked);
        }
    }
    m_updating = false;
    m_printButton->setText(tr("Print selected records (%1)").arg(selectedRecords().size()));
}

void PersonalizationPanel::addMappingRow(const QString &placeholder, const QString &column)
{
    const int row = m_mappingTable->rowCount();
    m_mappingTable->insertRow(row);

    auto *fieldItem = new QTableWidgetItem(placeholder);
    fieldItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
    m_mappingTable->setItem(row, 0, fieldItem);

    // A combo in the second column turns "type the column name exactly" into
    // "pick the column": a typo here used to mean blank cards with no warning.
    auto *combo = new QComboBox(m_mappingTable);
    combo->addItem(tr("(not mapped)"), QString());
    for (const QString &header : m_table.headers)
        combo->addItem(header, header);
    const int index = combo->findData(column);
    combo->setCurrentIndex(index < 0 ? 0 : index);
    m_mappingTable->setCellWidget(row, 1, combo);
    connect(combo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_updating)
            return;
        collectMappingFromTable();
        updateWarnings();
        updatePreview();
    });
}

void PersonalizationPanel::rebuildMappingTable()
{
    m_updating = true;
    m_mappingTable->clearContents();
    m_mappingTable->setRowCount(0);

    const QStringList placeholders = m_document ? m_document->placeholders() : QStringList();
    // Every placeholder the design uses, plus every mapping already configured:
    // a field that is no longer used stays visible instead of vanishing.
    QStringList fields = placeholders;
    for (const FieldMapping &mapping : m_mapping.fields) {
        if (!mapping.placeholder.isEmpty() && !fields.contains(mapping.placeholder))
            fields << mapping.placeholder;
    }
    for (const QString &field : fields)
        addMappingRow(field, m_mapping.columnFor(field));

    m_photoColumn->clear();
    m_photoColumn->addItem(tr("(no photo column)"), QString());
    for (const QString &header : m_table.headers)
        m_photoColumn->addItem(header, header);
    const int photoIndex = m_photoColumn->findData(m_mapping.photoAssetPlaceholder);
    if (photoIndex > 0)
        m_photoColumn->setCurrentIndex(photoIndex);
    m_updating = false;
}

void PersonalizationPanel::collectMappingFromTable()
{
    MappingSet rebuilt;
    rebuilt.photoAssetPlaceholder = m_mapping.photoAssetPlaceholder;
    rebuilt.relativePhotoBaseDir = m_mapping.relativePhotoBaseDir;

    for (int row = 0; row < m_mappingTable->rowCount(); ++row) {
        QTableWidgetItem *fieldItem = m_mappingTable->item(row, 0);
        if (!fieldItem)
            continue;
        const QString placeholder = fieldItem->text().trimmed();
        QString column;
        if (auto *combo = qobject_cast<QComboBox *>(m_mappingTable->cellWidget(row, 1)))
            column = combo->currentData().toString();
        else if (QTableWidgetItem *columnItem = m_mappingTable->item(row, 1))
            column = columnItem->text().trimmed();
        if (placeholder.isEmpty() || column.isEmpty())
            continue;
        rebuilt.fields.append(FieldMapping{ placeholder, column });
    }
    m_mapping = rebuilt;
}

void PersonalizationPanel::updateWarnings()
{
    if (!m_warningLabel)
        return;

    QStringList warnings;

    if (!m_table.isEmpty()) {
        const QStringList unmappedColumns = DataMapper::unmappedColumns(m_table, m_mapping);
        if (!unmappedColumns.isEmpty()) {
            warnings << tr("Columns that no card field uses: %1")
                            .arg(unmappedColumns.join(QStringLiteral(", ")));
        }
        if (m_document) {
            const QStringList unmappedFields =
                DataMapper::unmappedPlaceholders(m_document->placeholders(), m_mapping);
            if (!unmappedFields.isEmpty()) {
                warnings << tr("Card fields with no data column: %1 - they will print as they "
                               "are, placeholders included.")
                                .arg(unmappedFields.join(QStringLiteral(", ")));
            }
        }
        if (m_mapping.photoAssetPlaceholder.isEmpty()
            && !DataMapper::photoColumns(m_table).isEmpty()) {
            warnings << tr("This data set has a column that looks like a photograph (%1). "
                           "Choose it under \"Photo column\", or the photos will not be "
                           "inserted.")
                            .arg(DataMapper::photoColumns(m_table).join(QStringLiteral(", ")));
        }
    }

    if (warnings.isEmpty()) {
        m_warningLabel->setText(m_table.isEmpty()
                                    ? tr("Load a data set to map card fields to data columns.")
                                    : tr("Every card field is mapped and every data column is "
                                         "used."));
    } else {
        m_warningLabel->setText(QStringLiteral("! ") + warnings.join(QStringLiteral("\n! ")));
    }
    QPalette palette = m_warningLabel->palette();
    palette.setColor(QPalette::WindowText,
                     warnings.isEmpty() ? palette.color(QPalette::WindowText)
                                        : QColor(180, 110, 0));
    m_warningLabel->setPalette(palette);
}

void PersonalizationPanel::updatePreview()
{
    if (!m_preview)
        return;

    if (!m_document || m_table.isEmpty() || m_previewRow < 0) {
        m_preview->setPixmap(QPixmap());
        m_preview->setText(m_table.isEmpty()
                               ? tr("Load a data set to preview a personalised card.")
                               : tr("Select a record to preview it."));
        m_previewInfo->setText(QString());
        return;
    }

    // The photo for this record is registered with the render pass, never with
    // the document: previewing a thousand records must not grow the project.
    if (!m_mapping.photoAssetPlaceholder.isEmpty()) {
        bool missing = false;
        const QString photoPath = DataMapper::photoPathForRecord(m_table, m_previewRow,
                                                                 m_mapping, &missing);
        if (!photoPath.isEmpty()) {
            QString error;
            m_photoStore->addImageFile(photoPath, &error);
            if (!error.isEmpty()) {
                m_previewInfo->setText(tr("The photograph could not be read: %1").arg(error));
            }
        }
    }

    QString error;
    const QHash<QString, QString> values = DataMapper::valuesForRecord(m_table, m_previewRow,
                                                                       m_mapping, &error);
    if (!error.isEmpty()) {
        m_preview->setPixmap(QPixmap());
        m_preview->setText(error);
        return;
    }

    QVector<RenderContext::Warning> warnings;
    const double pxPerMm = kPreviewDpi / 25.4;
    const QImage image = BatchRenderer::renderRecord(*m_document, CardSideId::Front,
                                                    m_previewRow, m_table, m_mapping,
                                                    *m_photoStore, pxPerMm, &warnings);
    if (image.isNull()) {
        m_preview->setPixmap(QPixmap());
        m_preview->setText(tr("The record could not be rendered."));
        return;
    }

    const int width = qMax(220, m_preview->width() - 8);
    m_preview->setPixmap(QPixmap::fromImage(
        image.scaledToWidth(width, Qt::SmoothTransformation)));

    QStringList info;
    info << tr("Record %1").arg(m_previewRow + 1);
    for (const FieldMapping &mapping : m_mapping.fields) {
        const QString value = values.value(mapping.placeholder);
        info << QStringLiteral("%1: %2").arg(mapping.placeholder, value);
    }
    if (!m_mapping.photoAssetPlaceholder.isEmpty()) {
        info << tr("Photograph: %1")
                    .arg(values.value(m_mapping.photoAssetPlaceholder,
                                      tr("(none for this record)")));
    }
    for (const RenderContext::Warning &warning : warnings)
        info << tr("Warning: %1").arg(warning.message);
    m_previewInfo->setText(info.join(QLatin1Char('\n')));
}

void PersonalizationPanel::saveMapping()
{
    const QString suggested = m_table.sourcePath.isEmpty()
                                  ? QString()
                                  : m_table.sourcePath + QStringLiteral(".mapping.json");
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save mapping"), suggested, tr("Mapping files (*.json)"));
    if (path.isEmpty())
        return;

    collectMappingFromTable();
    QString error;
    if (!DataMapper::saveMapping(m_mapping, path, &error)) {
        QMessageBox::warning(this, tr("Save mapping"), error);
        emit statusMessage(tr("The mapping could not be saved: %1").arg(error));
        return;
    }
    emit statusMessage(tr("Mapping saved to \"%1\".").arg(QFileInfo(path).fileName()));
}

void PersonalizationPanel::loadMapping()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load mapping"),
        m_table.sourcePath.isEmpty() ? AppSettings::instance().lastOpenDirectory()
                                     : QFileInfo(m_table.sourcePath).absolutePath(),
        tr("Mapping files (*.json);;All files (*)"));
    if (path.isEmpty())
        return;

    MappingSet loaded;
    QString error;
    if (!DataMapper::loadMapping(&loaded, path, &error)) {
        QMessageBox::warning(this, tr("Load mapping"), error);
        emit statusMessage(tr("The mapping could not be loaded: %1").arg(error));
        return;
    }

    m_mapping = loaded;
    if (m_mapping.relativePhotoBaseDir.isEmpty() && !m_table.sourcePath.isEmpty())
        m_mapping.relativePhotoBaseDir = QFileInfo(m_table.sourcePath).absolutePath();
    rebuildMappingTable();
    updateWarnings();
    updatePreview();
    emit statusMessage(tr("Mapping loaded from \"%1\".").arg(QFileInfo(path).fileName()));
}

} // namespace occ
