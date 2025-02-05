#include "proj_lite_internal/database.h"

#include "proj_lite_internal/constants.h"

#define ARRAY_COUNT(x) (sizeof(x) / sizeof(x[0]))

static const pl_Ellipsoid ELLIPSOIDS[] = {
    {"MERIT", 6378137.0, 298.257, "MERIT 1983"},
    {"SGS85", 6378136.0, 298.257, "Soviet Geodetic System 85"},
    {"GRS80", 6378137.0, 298.257222101, "GRS 1980(IUGG, 1980)"},
    {"IAU76", 6378140.0, 298.257, "IAU 1976"},
    {"airy", 6377563.396, 299.3249646, "Airy 1830"},
    {"APL4.9", 6378137.0, 298.25, "Appl. Physics. 1965"},
    {"NWL9D", 6378145.0, 298.25, "Naval Weapons Lab., 1965"},
    {"mod_airy", 6377340.189, 299.3249646, "Modified Airy"},
    {"andrae", 6377104.43, 300.0, "Andrae 1876 (Den., Iclnd.)"},
    {"danish", 6377019.2563, 300.0, "Andrae 1876 (Denmark, Iceland)"},
    {"aust_SA", 6378160.0, 298.25, "Australian Natl & S. Amer. 1969"},
    {"GRS67", 6378160.0, 298.2471674270, "GRS 67(IUGG 1967)"},
    {"GSK2011", 6378136.5, 298.2564151, "GSK-2011"},
    {"bessel", 6377397.155, 299.1528128, "Bessel 1841"},
    {"bess_nam", 6377483.865, 299.1528128, "Bessel 1841 (Namibia)"},
    {"clrk66", 6378206.4, 294.978698213898, "Clarke 1866"},
    {"clrk80", 6378249.145, 293.4663, "Clarke 1880 mod."},
    {"clrk80ign", 6378249.2, 293.4660212936269, "Clarke 1880 (IGN)."},
    {"CPM", 6375738.7, 334.29, "Comm. des Poids et Mesures 1799"},
    {"delmbr", 6376428., 311.5, "Delambre 1810 (Belgium)"},
    {"engelis", 6378136.05, 298.2566, "Engelis 1985"},
    {"evrst30", 6377276.345, 300.8017, "Everest 1830"},
    {"evrst48", 6377304.063, 300.8017, "Everest 1948"},
    {"evrst56", 6377301.243, 300.8017, "Everest 1956"},
    {"evrst69", 6377295.664, 300.8017, "Everest 1969"},
    {"evrstSS", 6377298.556, 300.8017, "Everest (Sabah & Sarawak)"},
    {"fschr60", 6378166., 298.3, "Fischer (Mercury Datum) 1960"},
    {"fschr60m", 6378155., 298.3, "Modified Fischer 1960"},
    {"fschr68", 6378150., 298.3, "Fischer 1968"},
    {"helmert", 6378200., 298.3, "Helmert 1906"},
    {"hough", 6378270.0, 297., "Hough"},
    {"intl", 6378388.0, 297., "International 1909 (Hayford)"},
    {"krass", 6378245.0, 298.3, "Krassovsky, 1942"},
    {"kaula", 6378163., 298.24, "Kaula 1961"},
    {"lerch", 6378139., 298.257, "Lerch 1979"},
    {"mprts", 6397300., 191., "Maupertius 1738"},
    {"new_intl", 6378157.5, 298.249615390014, "New International 1967"},
    {"plessis", 6376523., 308.64, "Plessis 1817 (France)"},
    {"PZ90", 6378136.0, 298.25784, "PZ-90"},
    {"SEasia", 6378155.0, 298.300, "Southeast Asia"},
    {"walbeck", 6376896.0, 302.78, "Walbeck"},
    {"WGS60", 6378165.0, 298.3, "WGS 60"},
    {"WGS66", 6378145.0, 298.25, "WGS 66"},
    {"WGS72", 6378135.0, 298.26, "WGS 72"},
    {"WGS84", WGS84_A, WGS84_IF, "WGS 84"},
    {"sphere", 6370997.0, 0.0, "Normal Sphere (r=6370997)"}};

static const pl_PrimeMeridian PRIME_MERIDIANS[] = {
    {"greenwich", "Greenwich", 0},
    {"lisbon", "Lisbon", -0.159381828},
    {"paris", "Paris", 0.04079234433197663588},
    {"bogota", "Bogota", -1.292955915},
    {"madrid", "Madrid", -0.064366676},
    {"rome", "Rome", 0.217334216},
    {"bern", "Bern", 0.129845224},
    {"jakarta", "Jakarta", 1.864146371},
    {"ferro", "Ferro", -0.308341501},
    {"brussels", "Brussels", 0.076235545},
    {"stockholm", "Stockholm", 0.315176404},
    {"athens", "Athens", 0.413928176},
    {"oslo", "Oslo", 0.187150201}};

static const pl_MeasurementUnit LENGTH_UNITS[] = {
    {"km", "Kilometer", 1000.0},
    {"m", "Meter", 1.0},
    {"dm", "Decimeter", 0.1},
    {"cm", "Centimeter", 0.01},
    {"mm", "Millimeter", 0.001},
    {"kmi", "International Nautical Mile", 1852.0},
    {"in", "International Inch", 0.0254},
    {"ft", "International Foot", 0.3048},
    {"yd", "International Yard", 0.9144},
    {"mi", "International Statute Mile", 1609.344},
    {"fath", "International Fathom", 1.8288},
    {"ch", "International Chain", 20.1168},
    {"link", "International Link", 0.201168},
    {"us-in", "U.S. Surveyor's Inch", 100 / 3937.0},
    {"us-ft", "U.S. Surveyor's Foot", 1200 / 3937.0},
    {"us-yd", "U.S. Surveyor's Yard", 3600 / 3937.0},
    {"us-ch", "U.S. Surveyor's Chain", 79200 / 3937.0},
    {"us-mi", "U.S. Surveyor's Statute Mile", 6336000 / 3937.0},
    {"ind-yd", "Indian Yard", 0.91439523},
    {"ind-ft", "Indian Foot", 0.30479841},
    {"ind-ch", "Indian Chain", 20.11669506}};

static const pl_MeasurementUnit ANGLE_UNITS[] = {
    {"rad", "Radian", 1.0},
    {"deg", "Degree", 0.017453292519943296},
    {"grad", "Grad", 0.015707963267948967}};

static const pl_Datum DATUMS[] = {
    {"WGS84", "WGS84", 3, {0, 0, 0, 0, 0, 0, 0}},
    {"GGRS87", "GRS80", 3, {-199.87, 74.79, 246.62, 0, 0, 0, 0}},
    {"NAD83", "GRS80", 3, {0, 0, 0, 0, 0, 0, 0}},
    {"carthage", "clrk80ign", 3, {-263.0, 6.0, 431.0, 0, 0, 0, 0}},
    {"hermannskogel", "bessel", 7, {682.0, -203.0, 480.0, 0, 0, 0, 0}},
    {"ire65", "mod_airy", 7, {482.530, -130.596, 564.557, -1.042, -0.214, -0.631, 8.15}},
    {"nzgd49", "intl", 7, {59.47, -5.04, 187.44, 0.47, -0.1, 1.024, -4.5993}},
    {"OSGB36", "airy", 7, {446.448, -125.157, 542.060, 0.1502, 0.2470, 0.8421, -20.4894}},
};

#define FIND_DB(TYPE, FN_NAME, ARRAY)                  \
    const TYPE* FN_NAME(pl_StringSpan ref)             \
    {                                                  \
        for (int i = 0; i < ARRAY_COUNT(ARRAY); ++i)   \
        {                                              \
            if (pl_ss_compare(&ref, ARRAY[i].proj_id)) \
            {                                          \
                return ARRAY + i;                      \
            }                                          \
        }                                              \
        return NULL;                                   \
    }

FIND_DB(pl_Datum, pl_db_find_datum, DATUMS)
FIND_DB(pl_Ellipsoid, pl_db_find_ellipsoid, ELLIPSOIDS)
FIND_DB(pl_MeasurementUnit, pl_db_find_length_unit, LENGTH_UNITS)
FIND_DB(pl_MeasurementUnit, pl_db_find_angle_unit, ANGLE_UNITS)
FIND_DB(pl_PrimeMeridian, pl_db_find_prime_meridian, PRIME_MERIDIANS)
