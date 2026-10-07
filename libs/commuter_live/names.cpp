#include "commuter_live/names.hpp"

#include "commuter_live/types.hpp"

#include <algorithm>
#include <string_view>

namespace commuter_live
{
namespace
{
struct Name
{
  std::string_view m_code;
  std::string_view m_name;
};

// FGC stop_id values from the operator GTFS, matched to Geotren codes.
Name constexpr kStations[] = {
    {"AB", "Abrera"},
    {"AE", "Montserrat-Aeri"},
    {"AL", "Almeda"},
    {"BE", "La Beguda"},
    {"BN", "La Bonanova"},
    {"BO", "Sant Boi"},
    {"BT", "Bellaterra"},
    {"CA", "Capellades"},
    {"CB", "Castellbell i El Vilar"},
    {"CF", "Can Feu - Gràcia"},
    {"CG", "Colònia Güell"},
    {"CL", "Santa Coloma de Cervelló"},
    {"CO", "Cornellà Riera"},
    {"CP", "Can Parellada"},
    {"CR", "Can Ros"},
    {"CT", "La Creu Alta"},
    {"EN", "Terrassa Estació del nord"},
    {"EP", "El Putxet"},
    {"EU", "Europa | Fira"},
    {"FN", "Les Fonts"},
    {"GO", "Gornal"},
    {"GR", "Gràcia"},
    {"HG", "Hospital General"},
    {"IC", "Ildefons Cerdà"},
    {"IG", "Igualada"},
    {"LF", "La Floresta"},
    {"LH", "L'Hospitalet Av. Carrilet"},
    {"LP", "Les Planes"},
    {"MA", "Manresa-Alta"},
    {"MB", "Manresa-Baixador"},
    {"MC", "Martorell Central"},
    {"ME", "Martorell Enllaç"},
    {"MG", "Magòria La Campana"},
    {"ML", "Molí Nou - Ciutat Cooperativa"},
    {"MM", "Montserrat"},
    {"MN", "Muntaner"},
    {"MO", "Monistrol de Montserrat"},
    {"MP", "Monistrol-Vila"},
    {"MQ", "Masquefa"},
    {"MS", "Mira-Sol"},
    {"MV", "Martorell Vila"},
    {"NA", "Terrassa Nacions Unides"},
    {"NO", "Sabadell Nord"},
    {"OL", "Olesa de Montserrat"},
    {"PA", "Pallejà"},
    {"PC", "Barcelona - Plaça Catalunya"},
    {"PD", "Pàdua"},
    {"PE", "Barcelona - Plaça Espanya"},
    {"PF", "Peu del Funicular"},
    {"PI", "Piera"},
    {"PJ", "Sabadell Plaça Major"},
    {"PL", "El Palau"},
    {"PM", "Pl. Molina"},
    {"PN", "Sabadell Parc del Nord"},
    {"PO", "La Pobla de Claramunt"},
    {"PR", "Provença"},
    {"QC", "Quatre Camins"},
    {"RB", "Rubí Centre"},
    {"RE", "Reina Elisenda"},
    {"SA", "Sant Andreu de la Barca"},
    {"SC", "Sant Cugat Centre"},
    {"SE", "Sant Esteve Sesrovires"},
    {"SG", "Sant Gervasi"},
    {"SJ", "Sant Joan"},
    {"SP", "Sant Josep"},
    {"SQ", "Sant Quirze"},
    {"SR", "Sarrià"},
    {"SV", "Sant Vicenç-Castellgalí"},
    {"TB", "Av. Tibidabo"},
    {"TR", "Terrassa - Rambla"},
    {"TT", "Les Tres Torres"},
    {"UN", "Universitat Autònoma"},
    {"VA", "Vallbona d'Anoia"},
    {"VD", "Valldoreix"},
    {"VH", "Sant Vicenç dels Horts"},
    {"VI", "Manresa Viladordis"},
    {"VL", "Baixador de Vallvidrera"},
    {"VN", "Vilanova del Camí"},
    {"VO", "Volpelleres"},
    {"VP", "Vallparadís Universitat"},
};

struct Color
{
  std::string_view m_line;
  std::string_view m_color;
};

// FGC colours are route_color from the operator GTFS. Rodalies colours are the
// line swatches published for Rodalies de Catalunya. Express variants share the
// parent line colour.
Color constexpr kColors[] = {
    {"L12", "B2AED3"}, {"L6", "797FBC"},  {"L7", "B2600B"},  {"L8", "E274AA"},  {"M1", "000000"},  {"M2", "000000"},
    {"R1", "4499D4"},  {"R11", "0064A5"}, {"R12", "FFDC00"}, {"R13", "E52E87"}, {"R14", "675199"}, {"R15", "9A8A76"},
    {"R16", "AF0036"}, {"R17", "E97300"}, {"R2", "009900"},  {"R2N", "99C83E"}, {"R2S", "00642E"}, {"R3", "FF131A"},
    {"R4", "FF9221"},  {"R5", "3DBFC3"},  {"R50", "00738A"}, {"R53", "3DBFC3"}, {"R5R", "3DBFC3"}, {"R6", "B3B3B3"},
    {"R60", "5B5B5B"}, {"R61", "B3B3B3"}, {"R62", "B3B3B3"}, {"R63", "B3B3B3"}, {"R6R", "B3B3B3"}, {"R7", "BD7DB5"},
    {"R8", "9B1987"},  {"S1", "EF7900"},  {"S2", "88BB0B"},  {"S3", "4F868E"},  {"S4", "A78600"},  {"S8", "49C0DE"},
    {"S9", "DF4661"},
};

}  // namespace

bool InBarcelona(double lat, double lon)
{
  return lat >= 41.15 && lat <= 41.80 && lon >= 1.45 && lon <= 2.80;
}

std::string StationName(std::string_view code)
{
  if (code.empty())
    return {};
  auto const it = std::lower_bound(std::begin(kStations), std::end(kStations), code,
                                   [](Name const & entry, std::string_view value) { return entry.m_code < value; });
  if (it == std::end(kStations) || it->m_code != code)
    return std::string(code);
  return std::string(it->m_name);
}

std::string LineColor(std::string_view line)
{
  auto const it = std::lower_bound(std::begin(kColors), std::end(kColors), line,
                                   [](Color const & entry, std::string_view value) { return entry.m_line < value; });
  if (it == std::end(kColors) || it->m_line != line)
    return "888888";
  return std::string(it->m_color);
}
}  // namespace commuter_live
