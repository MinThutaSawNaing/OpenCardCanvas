#pragma once

#include "personalization/CsvImporter.h"
#include "personalization/DataMapper.h"

#include <QVector>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace occ {

class AssetStore;
class CardDocument;

// ---------------------------------------------------------------------------
// PersonalizationPanel - CSV data in, personalised cards out.
//
// The panel is built around one rule: nothing is guessed silently. A column
// that no placeholder uses is reported, a placeholder that no column feeds is
// reported, a photo column has to be chosen deliberately, and the per-record
// preview is rendered by BatchRenderer (which calls the same CardRenderer the
// printer uses) rather than by a preview-only drawing path.
//
// The data set never modifies the document. Rendering a record produces a copy,
// so browsing a thousand rows cannot damage the design.
// ---------------------------------------------------------------------------
class PersonalizationPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PersonalizationPanel(QWidget *parent = nullptr);
    ~PersonalizationPanel() override;

    void setDocument(CardDocument *document);
    // Loads a CSV file, reporting any parse error in the panel. Returns false
    // when the file could not be used.
    bool loadDataFile(const QString &path);

    int recordCount() const { return m_table.rowCount(); }
    QVector<int> selectedRecords() const;
    // Mapping currently in use, so the window can pass it on to a batch export.
    const MappingSet &mapping() const { return m_mapping; }
    const CsvTable &data() const { return m_table; }

signals:
    void printRequested(const QVector<int> &recordIndexes);
    void statusMessage(const QString &message);

private:
    void buildUi();
    void loadDataFileInteractive();
    void rebuildRecordTable();
    void rebuildMappingTable();
    void addMappingRow(const QString &placeholder, const QString &column);
    void collectMappingFromTable();
    void updateWarnings();
    void updatePreview();
    void setAllChecked(bool checked);
    void invertChecked();
    void saveMapping();
    void loadMapping();
    void reportError(const QString &error);

    CardDocument *m_document = nullptr;
    CsvTable      m_table;
    MappingSet    m_mapping;
    AssetStore   *m_photoStore = nullptr;
    bool          m_updating = false;
    int           m_previewRow = -1;

    QLabel      *m_fileLabel = nullptr;
    QLabel      *m_errorLabel = nullptr;
    QLabel      *m_warningLabel = nullptr;
    QLabel      *m_previewInfo = nullptr;
    QLabel      *m_preview = nullptr;
    QTableWidget *m_records = nullptr;
    QTableWidget *m_mappingTable = nullptr;
    QComboBox   *m_photoColumn = nullptr;
    QSpinBox    *m_previewCount = nullptr;
    QPushButton *m_printButton = nullptr;
};

} // namespace occ
