#include "NastranReader.h"

#include <vtkPoints.h>
#include <vtkTetra.h>
#include <vtkHexahedron.h>
#include <vtkWedge.h>
#include <vtkTriangle.h>
#include <vtkQuad.h>
#include <vtkIdList.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// Trim ASCII whitespace from both ends.
std::string trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string toUpper(std::string s)
{
    for (char &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool startsWith(const std::string &s, const char *prefix)
{
    return s.rfind(prefix, 0) == 0;
}

// Parse a Nastran real number. Nastran allows Fortran-style exponents without
// 'E': "1.0+5" means 1.0e5, "2.5-3" means 2.5e-3. Insert the missing 'E' before
// a sign that follows a digit/'.' and isn't the leading sign.
double parseReal(const std::string &raw)
{
    std::string f = trim(raw);
    if (f.empty()) return 0.0;

    std::string fixed;
    fixed.reserve(f.size() + 2);
    for (size_t i = 0; i < f.size(); ++i) {
        const char c = f[i];
        if ((c == '+' || c == '-') && i > 0) {
            const char prev = f[i - 1];
            // A sign following a digit or '.' (and not already after e/E) marks
            // an implicit exponent.
            if ((std::isdigit(static_cast<unsigned char>(prev)) || prev == '.') &&
                prev != 'e' && prev != 'E') {
                fixed.push_back('E');
            }
        }
        fixed.push_back(c);
    }
    return std::stod(fixed);
}

// One parsed card: name plus its data fields (field 0 is the name itself,
// stripped of any trailing '*'). Continuation fields are already appended.
struct Card
{
    std::string name;                 // e.g. "GRID", "CTETRA"
    std::vector<std::string> fields;  // data fields, index 1..N (index 0 unused)
};

// ── Tokeniser ────────────────────────────────────────────────────────────────
//
// Splits the bulk-data section into Cards, merging continuation lines. Each
// physical line is classified as small/large/free and split into 8 (or, for
// large field, 9) logical fields; the trailing continuation field is dropped
// and the next line's fields are appended to the same card.
class Tokenizer
{
public:
    explicit Tokenizer(std::vector<std::string> lines) : m_lines(std::move(lines)) {}

    std::vector<Card> parse()
    {
        std::vector<Card> cards;
        bool inBulk = false;

        for (std::string &rawLine : m_lines) {
            // Strip trailing CR (CRLF files) and comments.
            std::string line = rawLine;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && line[0] == '$') continue;  // full-line comment

            const std::string trimmed = trim(line);
            if (trimmed.empty()) continue;

            const std::string upper = toUpper(trimmed);
            if (!inBulk) {
                if (startsWith(upper, "BEGIN BULK")) inBulk = true;
                continue;  // skip executive/case control until BEGIN BULK
            }
            if (startsWith(upper, "ENDDATA")) break;

            const bool isContinuation = (line[0] == '+' || line[0] == '*' ||
                                         line[0] == ' ' || line[0] == '\t');

            std::vector<std::string> fields;
            const bool largeField = splitLine(line, fields);

            if (isContinuation && !cards.empty()) {
                // Append this line's data fields to the open card. For a
                // continuation, field 0 is the continuation marker — skip it.
                for (size_t i = 1; i < fields.size(); ++i)
                    cards.back().fields.push_back(fields[i]);
            } else {
                Card card;
                std::string name = toUpper(trim(fields.empty() ? "" : fields[0]));
                if (!name.empty() && name.back() == '*') name.pop_back();
                card.name = trim(name);
                card.fields.push_back("");  // index 0 placeholder
                for (size_t i = 1; i < fields.size(); ++i)
                    card.fields.push_back(fields[i]);
                cards.push_back(std::move(card));
            }
            (void)largeField;
        }
        return cards;
    }

private:
    // Split one physical line into fields. Returns true if it was large-field.
    // For fixed formats the final (10th / continuation) field is excluded — it
    // only holds the continuation tag, which we don't need since we merge by
    // line order.
    static bool splitLine(const std::string &line, std::vector<std::string> &out)
    {
        out.clear();

        // Free field: any comma present.
        if (line.find(',') != std::string::npos) {
            std::string cur;
            for (char c : line) {
                if (c == ',') { out.push_back(trim(cur)); cur.clear(); }
                else          { cur.push_back(c); }
            }
            out.push_back(trim(cur));
            // Drop a trailing continuation marker field if present.
            if (!out.empty()) {
                const std::string last = trim(out.back());
                if (!last.empty() && (last[0] == '+' || last[0] == '*'))
                    out.pop_back();
            }
            return false;
        }

        // Fixed field. Large field if the first 8-col field's name ends with '*'.
        const std::string first8 = line.substr(0, std::min<size_t>(8, line.size()));
        const bool large = (trim(first8).find('*') != std::string::npos) || (line[0] == '*');

        const size_t fieldWidth = large ? 16 : 8;
        // Field 1 (the name) is always 8 columns; subsequent fields are 8 or 16.
        out.push_back(line.size() >= 8 ? line.substr(0, 8) : line);

        size_t pos = 8;
        // Large/small both carry up to ~4 (large) or 8 (small) data fields per
        // line before the continuation field; read until the line ends.
        const size_t maxData = large ? 4 : 8;
        for (size_t i = 0; i < maxData && pos < line.size(); ++i) {
            const std::string f = line.substr(pos, std::min(fieldWidth, line.size() - pos));
            out.push_back(f);
            pos += fieldWidth;
        }
        return large;
    }

    std::vector<std::string> m_lines;
};

// Field accessor with bounds safety: returns "" for missing fields.
const std::string &fieldAt(const Card &c, size_t i)
{
    static const std::string empty;
    return (i < c.fields.size()) ? c.fields[i] : empty;
}

int parseInt(const std::string &raw)
{
    const std::string f = trim(raw);
    return f.empty() ? 0 : std::stoi(f);
}

} // namespace

vtkSmartPointer<vtkUnstructuredGrid>
NastranReader::read(const std::string &path, Stats *outStats)
{
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("NastranReader: cannot open '" + path + "'");

    std::vector<std::string> lines;
    {
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }

    Tokenizer tok(std::move(lines));
    const std::vector<Card> cards = tok.parse();

    // ── First pass: nodes ─────────────────────────────────────────────────────
    auto points = vtkSmartPointer<vtkPoints>::New();
    std::unordered_map<int, vtkIdType> idToIndex;  // Nastran node ID → point idx

    Stats stats;
    for (const Card &c : cards) {
        if (c.name != "GRID") continue;
        const int    id = parseInt(fieldAt(c, 1));
        // field 2 = CP (coordinate system); honoured only as basic system 0.
        const double x  = parseReal(fieldAt(c, 3));
        const double y  = parseReal(fieldAt(c, 4));
        const double z  = parseReal(fieldAt(c, 5));
        const vtkIdType idx = points->InsertNextPoint(x, y, z);
        idToIndex[id] = idx;
        ++stats.nodes;
    }

    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->SetPoints(points);
    grid->Allocate(static_cast<vtkIdType>(cards.size()));

    // Resolve a node ID to its point index, or -1 if undefined.
    auto resolve = [&](const std::string &raw, bool &ok) -> vtkIdType {
        const int nid = parseInt(raw);
        auto it = idToIndex.find(nid);
        if (it == idToIndex.end()) { ok = false; return -1; }
        return it->second;
    };

    // Insert an element given its connectivity node IDs (fields start at index 3,
    // after EID and PID). `count` node fields are read.
    auto insertCell = [&](const Card &c, int vtkCellType, int count,
                          vtkSmartPointer<vtkCell> cell) -> bool {
        bool ok = true;
        for (int i = 0; i < count; ++i) {
            const vtkIdType idx = resolve(fieldAt(c, 3 + i), ok);
            if (!ok) return false;
            cell->GetPointIds()->SetId(i, idx);
        }
        grid->InsertNextCell(vtkCellType, cell->GetPointIds());
        return true;
    };

    // ── Second pass: elements ──────────────────────────────────────────────────
    for (const Card &c : cards) {
        if (c.name == "GRID") continue;

        if (c.name == "CTETRA") {
            auto cell = vtkSmartPointer<vtkTetra>::New();
            if (insertCell(c, VTK_TETRA, 4, cell)) ++stats.volumeCells;
            else ++stats.skippedCards;
        } else if (c.name == "CHEXA") {
            auto cell = vtkSmartPointer<vtkHexahedron>::New();
            if (insertCell(c, VTK_HEXAHEDRON, 8, cell)) ++stats.volumeCells;
            else ++stats.skippedCards;
        } else if (c.name == "CPENTA") {
            auto cell = vtkSmartPointer<vtkWedge>::New();
            if (insertCell(c, VTK_WEDGE, 6, cell)) ++stats.volumeCells;
            else ++stats.skippedCards;
        } else if (c.name == "CTRIA3") {
            auto cell = vtkSmartPointer<vtkTriangle>::New();
            if (insertCell(c, VTK_TRIANGLE, 3, cell)) ++stats.shellCells;
            else ++stats.skippedCards;
        } else if (c.name == "CQUAD4") {
            auto cell = vtkSmartPointer<vtkQuad>::New();
            if (insertCell(c, VTK_QUAD, 4, cell)) ++stats.shellCells;
            else ++stats.skippedCards;
        } else {
            ++stats.unknownCards;
        }
    }

    if (outStats) *outStats = stats;

    if (grid->GetNumberOfCells() == 0)
        throw std::runtime_error("NastranReader: '" + path + "' produced no elements");

    return grid;
}
