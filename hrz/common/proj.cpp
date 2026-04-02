#include "hrz/common/proj.h"

namespace hrz_proj
{

pl_Transform wmerc_to_ecef;
pl_Transform wmerc_to_lonlat_deg;
pl_Transform wmerc_to_lonlat_rad;
pl_Transform ecef_to_wmerc;
pl_Transform lonlat_deg_to_wmerc;
pl_Transform lonlat_rad_to_wmerc;
pl_Transform lonlat_deg_to_ecef;
pl_Transform lonlat_rad_to_ecef;

pl_Crs wmerc;
pl_Crs ecef;
pl_Crs lonlat_deg;
pl_Crs lonlat_rad;

void init_global_common_transforms()
{
    pl_crs_from_proj_zstr(wmerc_proj_str, &wmerc);
    pl_crs_from_proj_zstr(ecef_proj_str, &ecef);
    pl_crs_from_proj_zstr(lonlat_deg_proj_str, &lonlat_deg);
    pl_crs_from_proj_zstr(lonlat_rad_proj_str, &lonlat_rad);

    pl_bake_transform(&wmerc, &ecef, &wmerc_to_ecef);
    pl_bake_transform(&wmerc, &lonlat_deg, &wmerc_to_lonlat_deg);
    pl_bake_transform(&wmerc, &lonlat_rad, &wmerc_to_lonlat_rad);
    pl_bake_transform(&ecef, &wmerc, &ecef_to_wmerc);
    pl_bake_transform(&lonlat_deg, &wmerc, &lonlat_deg_to_wmerc);
    pl_bake_transform(&lonlat_rad, &wmerc, &lonlat_rad_to_wmerc);
    pl_bake_transform(&lonlat_deg, &ecef, &lonlat_deg_to_ecef);
    pl_bake_transform(&lonlat_rad, &ecef, &lonlat_rad_to_ecef);
}

} // namespace hrz_proj
