// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#include "common.h"

TEST(from_proj, generated_line_3)
{
    double src_x[3] = {2.000000, 1.999990, 2.000010};
    double src_y[3] = {49.000000, 48.999990, 49.000010};
    double src_z[3] = {0.000000, 0.000000, 0.000000};
    double dst_x[3] = {2.0000000000000000, 1.9999899999999999, 2.0000100000000001};
    double dst_y[3] = {49.0000000000000000, 48.9999899999999968, 49.0000100000000103};
    double dst_z[3] = {0.0000000000000000, 0.0000000000000000, 0.0000000000000000};
    run_single_case_3d(
        "+proj=longlat +datum=WGS84", "+proj=longlat +datum=WGS84", 3, src_x, src_y, src_z, dst_x,
        dst_y, dst_z, 1e-7);
}

TEST(from_proj, generated_line_7)
{
    double src_x[3] = {182.000000, 181.999990, 182.000010};
    double src_y[3] = {49.000000, 48.999990, 49.000010};
    double src_z[3] = {0.000000, 0.000000, 0.000000};
    double dst_x[3] = {182.0000000000000000, 181.9999899999999968, 182.0000100000000032};
    double dst_y[3] = {49.0000000000000000, 48.9999899999999968, 49.0000100000000103};
    double dst_z[3] = {0.0000000000000000, 0.0000000000000000, 0.0000000000000000};
    run_single_case_3d(
        "+proj=longlat +datum=WGS84", "+proj=longlat +datum=WGS84", 3, src_x, src_y, src_z, dst_x,
        dst_y, dst_z, 1e-7);
}

TEST(from_proj, generated_line_11)
{
    double src_x[3] = {90.000000, 89.999990, 90.000010};
    double src_y[3] = {0.000000, -0.000010, 0.000010};
    double src_z[3] = {10.000000, 10.000000, 10.000000};
    double dst_x[3] = {90.0000000000000000, 89.9999900000015600, 90.0000099999984400};
    double dst_y[3] = {0.0000000000000000, -0.0000099999984216, 0.0000099999984216};
    double dst_z[3] = {11.0000000000000000, 10.9999999990686774, 10.9999999990686774};
    run_single_case_3d(
        "+proj=longlat +ellps=WGS84 +towgs84=0,1,0", "+proj=longlat +datum=WGS84", 3, src_x, src_y,
        src_z, dst_x, dst_y, dst_z, 1e-7);
}

TEST(from_proj, generated_line_15)
{
    double src_x[3] = {90.000000, 89.999990, 90.000010};
    double src_y[3] = {0.000000, -0.000010, 0.000010};
    double src_z[3] = {10.000000, 10.000000, 10.000000};
    double dst_x[3] = {90.0000000000000000, 89.9999900000015600, 90.0000099999984400};
    double dst_y[3] = {0.0000000000000000, -0.0000099999983879, 0.0000099999983879};
    double dst_z[3] = {14.1890735002234578, 14.1890734992921352, 14.1890734992921352};
    run_single_case_3d(
        "+proj=longlat +ellps=WGS84 +towgs84=0,1,0,0,0,0,0.5", "+proj=longlat +datum=WGS84", 3,
        src_x, src_y, src_z, dst_x, dst_y, dst_z, 1e-7);
}

TEST(from_proj, generated_line_19)
{
    double src_x[3] = {90.000000, 89.999990, 90.000010};
    double src_y[3] = {0.000000, -0.000010, 0.000010};
    double src_z[3] = {11.000000, 11.000000, 11.000000};
    double dst_x[3] = {90.0000000000000000, 89.9999899999984336, 90.0000100000015664};
    double dst_y[3] = {0.0000000000000000, -0.0000100000015784, 0.0000100000015784};
    double dst_z[3] = {10.0000000000000000, 9.9999999990686774, 9.9999999990686774};
    run_single_case_3d(
        "+proj=longlat +datum=WGS84", "+proj=longlat +ellps=WGS84 +towgs84=0,1,0", 3, src_x, src_y,
        src_z, dst_x, dst_y, dst_z, 1e-7);
}

TEST(from_proj, generated_line_23)
{
    double src_x[3] = {90.000000, 89.999990, 90.000010};
    double src_y[3] = {0.000000, -0.000010, 0.000010};
    double src_z[3] = {14.189074, 14.189074, 14.189074};
    double dst_x[3] = {90.0000000000000000, 89.9999899999984336, 90.0000100000015664};
    double dst_y[3] = {0.0000000000000000, -0.0000100000016121, 0.0000100000016121};
    double dst_z[3] = {10.0000005001202226, 10.0000005019828677, 10.0000005019828677};
    run_single_case_3d(
        "+proj=longlat +datum=WGS84", "+proj=longlat +ellps=WGS84 +towgs84=0,1,0,0,0,0,0.5", 3,
        src_x, src_y, src_z, dst_x, dst_y, dst_z, 1e-7);
}

TEST(from_proj, generated_line_27)
{
    double src_x[3] = {49.000000, 48.999990, 49.000010};
    double src_y[3] = {2.000000, 1.999990, 2.000010};
    double src_z[3] = {0.000000, 0.000000, 0.000000};
    double dst_x[3] = {2.0000000000000000, 1.9999899999999999, 2.0000100000000001};
    double dst_y[3] = {49.0000000000000000, 48.9999899999999968, 49.0000100000000103};
    double dst_z[3] = {0.0000000000000000, 0.0000000000000000, 0.0000000000000000};
    run_single_case_3d(
        "+proj=longlat +datum=WGS84 +axis=neu", "+proj=longlat +datum=WGS84", 3, src_x, src_y,
        src_z, dst_x, dst_y, dst_z, 1e-7);
}

TEST(from_proj, generated_line_31)
{
    double src_x[3] = {49.000000, 48.999990, 49.000010};
    double src_y[3] = {2.000000, 1.999990, 2.000010};
    double src_z[3] = {-1.000000, -1.000000, -1.000000};
    double dst_x[3] = {-2.0000000000000000, -1.9999899999999999, -2.0000100000000001};
    double dst_y[3] = {-49.0000000000000000, -48.9999899999999968, -49.0000100000000103};
    double dst_z[3] = {1.0000000000000000, 1.0000000000000000, 1.0000000000000000};
    run_single_case_3d(
        "+proj=longlat +datum=WGS84 +axis=swd", "+proj=longlat +datum=WGS84", 3, src_x, src_y,
        src_z, dst_x, dst_y, dst_z, 1e-7);
}

TEST(from_proj, generated_line_35)
{
    double src_x[3] = {2.000000, 1.999990, 2.000010};
    double src_y[3] = {49.000000, 48.999990, 49.000010};
    double src_z[3] = {1.000000, 1.000000, 1.000000};
    double dst_x[3] = {49.0000000000000000, 48.9999899999999968, 49.0000100000000103};
    double dst_y[3] = {2.0000000000000000, 1.9999899999999999, 2.0000100000000001};
    double dst_z[3] = {1.0000000000000000, 1.0000000000000000, 1.0000000000000000};
    run_single_case_3d(
        "+proj=longlat +datum=WGS84", "+proj=longlat +datum=WGS84 +axis=neu", 3, src_x, src_y,
        src_z, dst_x, dst_y, dst_z, 1e-7);
}

TEST(from_proj, generated_line_39)
{
    double src_x[3] = {2.000000, 1.999990, 2.000010};
    double src_y[3] = {49.000000, 48.999990, 49.000010};
    double src_z[3] = {1.000000, 1.000000, 1.000000};
    double dst_x[3] = {-49.0000000000000000, -48.9999899999999968, -49.0000100000000103};
    double dst_y[3] = {-2.0000000000000000, -1.9999899999999999, -2.0000100000000001};
    double dst_z[3] = {-1.0000000000000000, -1.0000000000000000, -1.0000000000000000};
    run_single_case_3d(
        "+proj=longlat +datum=WGS84", "+proj=longlat +datum=WGS84 +axis=swd", 3, src_x, src_y,
        src_z, dst_x, dst_y, dst_z, 1e-7);
}

TEST(geocent, generated_line_3)
{
    double src_x[3] = {0.000000, -1.000000, 1.000000};
    double src_y[3] = {0.000000, -1.000000, 1.000000};
    double src_z[3] = {0.000000, 0.000000, 0.000000};
    double dst_x[3] = {
        6378137.0000000000000000, 6376200.8062320351600647, 6376200.8062320351600647
    };
    double dst_y[3] = {0.0000000000000000, -111296.9990681334747933, 111296.9990681334747933};
    double dst_z[3] = {0.0000000000000000, -110568.7748245666443836, 110568.7748245666443836};
    run_single_case_3d(
        "+proj=latlong +datum=WGS84 +no_defs", "+proj=geocent +datum=WGS84 +no_defs", 3, src_x,
        src_y, src_z, dst_x, dst_y, dst_z, 0.01);
}

TEST(geocent, generated_line_4)
{
    double src_x[3] = {0.000000, -1.000000, 1.000000};
    double src_y[3] = {0.000000, -1.000000, 1.000000};
    double src_z[3] = {0.000000, 0.000000, 0.000000};
    double dst_x[3] = {
        6378400.0000000018626451, 6376463.8062320351600647, 6376463.8062320351600647
    };
    double dst_y[3] = {-6.0000000000000018, -111302.9990681334893452, 111290.9990681334602414};
    double dst_z[3] = {-431.0000000000001705, -110999.7748245666734874, 110137.7748245666443836};
    run_single_case_3d(
        "+proj=latlong +datum=WGS84 +no_defs", "+proj=geocent +datum=carthage +no_defs", 3, src_x,
        src_y, src_z, dst_x, dst_y, dst_z, 0.01);
}

TEST(geocent, generated_line_8)
{
    double src_x[3] = {0.000000, -1.000000, 1.000000};
    double src_y[3] = {0.000000, -1.000000, 1.000000};
    double src_z[3] = {10.000000, 10.000000, 10.000000};
    double dst_x[3] = {
        6378147.0000000000000000, 6376210.8031861698254943, 6376210.8031861698254943
    };
    double dst_y[3] = {0.0000000000000000, -111297.1735656169767026, 111297.1735656169767026};
    double dst_z[3] = {0.0000000000000000, -110568.9493486310238950, 110568.9493486310238950};
    run_single_case_3d(
        "+proj=latlong +datum=WGS84 +no_defs", "+proj=geocent +datum=WGS84 +no_defs", 3, src_x,
        src_y, src_z, dst_x, dst_y, dst_z, 0.01);
}

TEST(geocent, generated_line_9)
{
    double src_x[3] = {0.000000, -1.000000, 1.000000};
    double src_y[3] = {0.000000, -1.000000, 1.000000};
    double src_z[3] = {10.000000, 10.000000, 10.000000};
    double dst_x[3] = {
        6378410.0000000000000000, 6376473.8031861698254943, 6376473.8031861707568169
    };
    double dst_y[3] = {-6.0000000000000000, -111303.1735656169767026, 111291.1735656169912545};
    double dst_z[3] = {-431.0000000000000000, -110999.9493486310238950, 110137.9493486310238950};
    run_single_case_3d(
        "+proj=latlong +datum=WGS84 +no_defs", "+proj=geocent +datum=carthage +no_defs", 3, src_x,
        src_y, src_z, dst_x, dst_y, dst_z, 0.01);
}

TEST(geocent, generated_line_13)
{
    double src_x[3] = {638000.000000, 637999.999990, 638000.000010};
    double src_y[3] = {0.000000, -0.000010, 0.000010};
    double src_z[3] = {0.000000, 0.000000, 0.000000};
    double dst_x[3] = {0.0000000000000000, -0.0000000008980530, 0.0000000008980530};
    double dst_y[3] = {0.0000000000000000, 0.0000000000000000, 0.0000000000000000};
    double dst_z[3] = {
        -5740137.0000000000000000, -5740137.0000099996104836, -5740136.9999900003895164
    };
    run_single_case_3d(
        "+proj=geocent +datum=WGS84 +no_defs", "+proj=latlong +datum=WGS84 +no_defs", 3, src_x,
        src_y, src_z, dst_x, dst_y, dst_z, 1e-7);
}

TEST(geocent, generated_line_14)
{
    double src_x[3] = {638000.000000, 637999.999990, 638000.000010};
    double src_y[3] = {0.000000, -0.000010, 0.000010};
    double src_z[3] = {0.000000, 0.000000, 0.000000};
    double dst_x[3] = {-0.0005386097534533, -0.0005386106511447, -0.0005386088557619};
    double dst_y[3] = {-0.0415124847277065, -0.0415124847284043, -0.0415124847270087};
    double dst_z[3] = {
        -5739986.0438356744125485, -5739986.0438456740230322, -5739986.0438256748020649
    };
    run_single_case_3d(
        "+proj=geocent +datum=WGS84 +no_defs", "+proj=latlong +datum=carthage +no_defs", 3, src_x,
        src_y, src_z, dst_x, dst_y, dst_z, 1e-7);
}

TEST(geocent, generated_line_18)
{
    double src_x[3] = {638000.000000, 637999.000000, 638001.000000};
    double src_y[3] = {0.000000, -1.000000, 1.000000};
    double src_z[3] = {0.000000, 0.000000, 0.000000};
    double dst_x[3] = {638000.0000000000000000, 637999.0000000004656613, 638001.0000000004656613};
    double dst_y[3] = {0.0000000000000000, -1.0000000000000007, 1.0000000000000007};
    double dst_z[3] = {0.0000000000000000, 0.0000000000000000, 0.0000000000000000};
    run_single_case_3d(
        "+proj=geocent +datum=WGS84 +no_defs", "+proj=geocent +datum=WGS84 +no_defs", 3, src_x,
        src_y, src_z, dst_x, dst_y, dst_z, 0.01);
}

TEST(geocent, generated_line_19)
{
    double src_x[3] = {638000.000000, 637999.000000, 638001.000000};
    double src_y[3] = {0.000000, -1.000000, 1.000000};
    double src_z[3] = {0.000000, 0.000000, 0.000000};
    double dst_x[3] = {638262.9999999996507540, 638261.9999999998835847, 638264.0000000012805685};
    double dst_y[3] = {-5.9999999999999973, -6.9999999999999947, -5.0000000000000053};
    double dst_z[3] = {-430.9999999999998295, -430.9999999999996589, -431.0000000000002274};
    run_single_case_3d(
        "+proj=geocent +datum=WGS84 +no_defs", "+proj=geocent +datum=carthage +no_defs", 3, src_x,
        src_y, src_z, dst_x, dst_y, dst_z, 0.01);
}

TEST(geocent, generated_line_23)
{
    double src_x[3] = {638000.000000, 637999.000000, 638001.000000};
    double src_y[3] = {0.000000, -1.000000, 1.000000};
    double src_z[3] = {0.000000, 0.000000, 0.000000};
    double dst_x[3] = {
        -159424.2913156408467330, -159425.0097107746987604, -159423.5729205071984325
    };
    double dst_y[3] = {
        -617760.3866698687197641, -617759.1685120667098090, -617761.6048276716610417
    };
    double dst_z[3] = {0.0000000000000000, 0.0000000000000000, 0.0000000000000000};
    run_single_case_3d(
        "+proj=geocent +datum=WGS84 +pm=paris +no_defs",
        "+proj=geocent +datum=WGS84 +pm=jakarta +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        dst_z, 0.01);
}

TEST(geocent, generated_line_24)
{
    double src_x[3] = {638000.000000, 637999.000000, 638001.000000};
    double src_y[3] = {0.000000, -1.000000, 1.000000};
    double src_z[3] = {0.000000, 0.000000, 0.000000};
    double dst_x[3] = {637861.0867494978010654, 637860.0519417632604018, 637862.1215572346700355};
    double dst_y[3] = {-22634.0412452241216670, -22635.0051813989011862, -22633.0773090494221833};
    double dst_z[3] = {-430.9999999999997726, -431.0000000000007958, -430.9999999999997726};
    run_single_case_3d(
        "+proj=geocent +datum=WGS84 +pm=paris +no_defs",
        "+proj=geocent +datum=carthage +pm=brussels +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        dst_z, 0.01);
}

TEST(geocent, generated_line_28)
{
    double src_x[3] = {0.000000, -1.000000, 1.000000};
    double src_y[3] = {0.000000, -1.000000, 1.000000};
    double src_z[3] = {10.000000, 10.000000, 10.000000};
    double dst_x[3] = {
        -1373934.2814255480188876, -1482201.4594887157436460, -1264832.9403435254935175
    };
    double dst_y[3] = {
        -6228407.8016723236069083, -6202542.2288769008591771, -6250491.8926860196515918
    };
    double dst_z[3] = {0.0000000000000000, -110568.9493486310238950, 110568.9493486310238950};
    run_single_case_3d(
        "+proj=latlong +datum=WGS84 +pm=brussels +no_defs",
        "+proj=geocent +datum=WGS84 +pm=jakarta +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        dst_z, 0.01);
}

TEST(geocent, generated_line_29)
{
    double src_x[3] = {0.000000, -1.000000, 1.000000};
    double src_y[3] = {0.000000, -1.000000, 1.000000};
    double src_z[3] = {10.000000, 10.000000, 10.000000};
    double dst_x[3] = {
        6374403.7763838963583112, 6376412.6978320544585586, 6368524.8933435892686248
    };
    double dst_y[3] = {225997.8984733054821845, 114702.0138395414251136, 337156.5618153741233982};
    double dst_z[3] = {-431.0000000000001705, -110999.9493486309802392, 110137.9493486310384469};
    run_single_case_3d(
        "+proj=latlong +datum=WGS84 +pm=brussels +no_defs",
        "+proj=geocent +datum=carthage +pm=paris +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        dst_z, 0.01);
}

TEST(geocent, generated_line_33)
{
    double src_x[3] = {638000.000000, 637999.000000, 638001.000000};
    double src_y[3] = {0.000000, -1.000000, 1.000000};
    double src_z[3] = {0.000000, 0.000000, 0.000000};
    double dst_x[3] = {42.0000000000000000, 41.9999101945621973, 42.0000898051562856};
    double dst_y[3] = {0.0000000000000000, 0.0000000000000000, 0.0000000000000000};
    double dst_z[3] = {
        -5740137.0000000000000000, -5740137.9999992158263922, -5740135.9999992158263922
    };
    run_single_case_3d(
        "+proj=geocent +datum=WGS84 +pm=42 +no_defs", "+proj=latlong +datum=WGS84 +pm=0 +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, dst_z, 0.01);
}

TEST(latlong_both_datum, generated_line_3)
{
    double src_x[3] = {10.000000, 9.999999, 10.000001};
    double src_y[3] = {60.000000, 59.999999, 60.000001};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {10.0024019638273458, 10.0024009637260445, 10.0024029639286489};
    double dst_y[3] = {60.0002006662784453, 60.0001996662943284, 60.0002016662625692};
    run_single_case(
        "+proj=longlat +ellps=clrk80 +towgs84=0,0,0,0,0,0,0 +no_defs",
        "+proj=longlat +ellps=intl +towgs84=114,-116,-333,0,0,0,0 +no_defs", 3, src_x, src_y, src_z,
        dst_x, dst_y, 1e-8);
}

TEST(latlong_both_datum, generated_line_7)
{
    double src_x[3] = {10.000000, 9.999999, 10.000001};
    double src_y[3] = {60.000000, 59.999999, 60.000001};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {9.9975981537176359, 9.9975971538189317, 9.9975991536163349};
    double dst_y[3] = {59.9997992933379152, 59.9997982933220513, 59.9998002933538004};
    run_single_case(
        "+proj=longlat +ellps=intl +towgs84=114,-116,-333,0,0,0,0 +no_defs",
        "+proj=longlat +ellps=clrk80 +towgs84=0,0,0,0,0,0,0 +no_defs", 3, src_x, src_y, src_z,
        dst_x, dst_y, 1e-8);
}

TEST(latlong_no_datum_both, generated_line_3)
{
    double src_x[3] = {10.000000, 9.999999, 10.000001};
    double src_y[3] = {60.000000, 59.999999, 60.000001};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {10.0000000000000000, 9.9999990000000007, 10.0000009999999993};
    double dst_y[3] = {59.9999999999999929, 59.9999990000000025, 60.0000009999999975};
    run_single_case(
        "+proj=longlat +ellps=clrk80 +no_defs", "+proj=longlat +ellps=intl +no_defs", 3, src_x,
        src_y, src_z, dst_x, dst_y, 1e-8);
}

TEST(latlong_no_datum_both, generated_line_7)
{
    double src_x[3] = {10.000000, 9.999999, 10.000001};
    double src_y[3] = {60.000000, 59.999999, 60.000001};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {10.0000000000000000, 9.9999990000000007, 10.0000009999999993};
    double dst_y[3] = {59.9999999999999929, 59.9999990000000025, 60.0000009999999975};
    run_single_case(
        "+proj=longlat +ellps=intl +no_defs", "+proj=longlat +ellps=clrk80 +no_defs", 3, src_x,
        src_y, src_z, dst_x, dst_y, 1e-8);
}

TEST(latlong_nzgd49, generated_line_6)
{
    double src_x[3] = {172.000000, 171.999990, 172.000010};
    double src_y[3] = {-43.000000, -43.000010, -42.999990};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {172.0000000000000000, 171.9999899999999968, 172.0000100000000032};
    double dst_y[3] = {-43.0000000000000000, -43.0000100000000032, -42.9999899999999968};
    run_single_case(
        "+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs",
        "+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        1e-7);
}

TEST(latlong_nzgd49, generated_line_7)
{
    double src_x[3] = {172.000000, 171.999990, 172.000010};
    double src_y[3] = {-43.000000, -43.000010, -42.999990};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {171.9998791876126347, 171.9998691877914041, 171.9998891874338938};
    double dst_y[3] = {-43.0016781080265034, -43.0016881078019750, -43.0016681082510459};
    run_single_case(
        "+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs", "+proj=longlat +datum=nzgd49 +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(latlong_nzgd49, generated_line_11)
{
    double src_x[3] = {172.000000, 171.999990, 172.000010};
    double src_y[3] = {-43.000000, -43.000010, -42.999990};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {172.0001208223987135, 172.0001108222199093, 172.0001308225774324};
    double dst_y[3] = {-42.9983218516136461, -42.9983318518381523, -42.9983118513891469};
    run_single_case(
        "+proj=longlat +datum=nzgd49 +no_defs", "+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(latlong_nzgd49, generated_line_12)
{
    double src_x[3] = {172.000000, 171.999990, 172.000010};
    double src_y[3] = {-43.000000, -43.000010, -42.999990};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {172.0000000000000000, 171.9999899999999968, 172.0000100000000032};
    double dst_y[3] = {-43.0000000000000000, -43.0000100000000032, -42.9999899999999968};
    run_single_case(
        "+proj=longlat +datum=nzgd49 +no_defs", "+proj=longlat +datum=nzgd49 +no_defs", 3, src_x,
        src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(latlong_one_datum, generated_line_3)
{
    double src_x[3] = {10.000000, 9.999999, 10.000001};
    double src_y[3] = {60.000000, 59.999999, 60.000001};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {10.0000000000000000, 9.9999990000000007, 10.0000009999999993};
    double dst_y[3] = {59.9999999999999929, 59.9999990000000025, 60.0000009999999975};
    run_single_case(
        "+proj=longlat +ellps=clrk80 +no_defs",
        "+proj=longlat +ellps=intl +towgs84=114,-116,-333,0,0,0,0 +no_defs", 3, src_x, src_y, src_z,
        dst_x, dst_y, 1e-8);
}

TEST(latlong_one_datum, generated_line_7)
{
    double src_x[3] = {10.000000, 9.999999, 10.000001};
    double src_y[3] = {60.000000, 59.999999, 60.000001};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {10.0000000000000000, 9.9999990000000007, 10.0000009999999993};
    double dst_y[3] = {59.9999999999999929, 59.9999990000000025, 60.0000009999999975};
    run_single_case(
        "+proj=longlat +ellps=clrk80 +towgs84=0,0,0,0,0,0,0 +no_defs",
        "+proj=longlat +ellps=intl +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-8);
}

TEST(latlong_one_datum, generated_line_11)
{
    double src_x[3] = {10.000000, 9.999999, 10.000001};
    double src_y[3] = {60.000000, 59.999999, 60.000001};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {10.0000000000000000, 9.9999990000000007, 10.0000009999999993};
    double dst_y[3] = {59.9999999999999929, 59.9999990000000025, 60.0000009999999975};
    run_single_case(
        "+proj=longlat +ellps=intl +towgs84=114,-116,-333,0,0,0,0 +no_defs",
        "+proj=longlat +ellps=clrk80 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-8);
}

TEST(latlong_one_datum, generated_line_15)
{
    double src_x[3] = {10.000000, 9.999999, 10.000001};
    double src_y[3] = {60.000000, 59.999999, 60.000001};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {10.0000000000000000, 9.9999990000000007, 10.0000009999999993};
    double dst_y[3] = {59.9999999999999929, 59.9999990000000025, 60.0000009999999975};
    run_single_case(
        "+proj=longlat +ellps=intl +no_defs",
        "+proj=longlat +ellps=clrk80 +towgs84=0,0,0,0,0,0,0 +no_defs", 3, src_x, src_y, src_z,
        dst_x, dst_y, 1e-8);
}

TEST(latlong_one_datum_symbolic, generated_line_3)
{
    double src_x[3] = {10.000000, 9.999999, 10.000001};
    double src_y[3] = {60.000000, 59.999999, 60.000001};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {10.0000000000000000, 9.9999990000000007, 10.0000009999999993};
    double dst_y[3] = {59.9999999999999929, 59.9999990000000025, 60.0000009999999975};
    run_single_case(
        "+proj=longlat +ellps=clrk80 +no_defs", "+proj=longlat +datum=nzgd49 +no_defs", 3, src_x,
        src_y, src_z, dst_x, dst_y, 1e-8);
}

TEST(latlong_one_datum_symbolic, generated_line_7)
{
    double src_x[3] = {10.000000, 9.999999, 10.000001};
    double src_y[3] = {60.000000, 59.999999, 60.000001};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {10.0000000000000000, 9.9999990000000007, 10.0000009999999993};
    double dst_y[3] = {59.9999999999999929, 59.9999990000000025, 60.0000009999999975};
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs", "+proj=longlat +ellps=intl +no_defs", 3, src_x,
        src_y, src_z, dst_x, dst_y, 1e-8);
}

TEST(latlong_osgb36, generated_line_6)
{
    double src_x[3] = {1.000000, 0.999990, 1.000010};
    double src_y[3] = {52.000000, 51.999990, 52.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {1.0000000000000000, 0.9999900000000000, 1.0000100000000001};
    double dst_y[3] = {52.0000000000000000, 51.9999899999999968, 52.0000100000000032};
    run_single_case(
        "+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs",
        "+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        1e-7);
}

TEST(latlong_osgb36, generated_line_7)
{
    double src_x[3] = {1.000000, 0.999990, 1.000010};
    double src_y[3] = {52.000000, 51.999990, 52.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {1.0017564105873262, 1.0017464089922923, 1.0017664121823617};
    double dst_y[3] = {51.9995279412350513, 51.9995179402480403, 51.9995379422220765};
    run_single_case(
        "+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs", "+proj=longlat +datum=OSGB36 +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(latlong_osgb36, generated_line_11)
{
    double src_x[3] = {1.000000, 0.999990, 1.000010};
    double src_y[3] = {52.000000, 51.999990, 52.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {0.9982437822957945, 0.9982337838905825, 0.9982537807010063};
    double dst_y[3] = {52.0004719696696682, 52.0004619706566089, 52.0004819686827204};
    run_single_case(
        "+proj=longlat +datum=OSGB36 +no_defs", "+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(latlong_osgb36, generated_line_12)
{
    double src_x[3] = {1.000000, 0.999990, 1.000010};
    double src_y[3] = {52.000000, 51.999990, 52.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {1.0000000000000000, 0.9999900000000000, 1.0000100000000001};
    double dst_y[3] = {52.0000000000000000, 51.9999899999999968, 52.0000100000000032};
    run_single_case(
        "+proj=longlat +datum=OSGB36 +no_defs", "+proj=longlat +datum=OSGB36 +no_defs", 3, src_x,
        src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_3)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {10.0000000000000000, 9.9999900000000004, 10.0000099999999996};
    double dst_y[3] = {59.9999999999999929, 59.9999899999999968, 60.0000100000000103};
    run_single_case(
        "+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs",
        "+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        1e-7);
}

TEST(latlong_world, generated_line_4)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {7.6669027851879061, 7.6668927843000212, 7.6669127860757884};
    double dst_y[3] = {60.0008103785718205, 60.0008003792134943, 60.0008203779301681};
    run_single_case(
        "+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs",
        "+proj=longlat +a=6378249.2 +b=6356515 +towgs84=-73,-247,227,0,0,0,0 +pm=paris +no_defs", 3,
        src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_5)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {10.0024021048396197, 10.0023921038265211, 10.0024121058527147};
    double dst_y[3] = {60.0029217842677980, 60.0029117849763836, 60.0029317835592337};
    run_single_case(
        "+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs",
        "+proj=longlat +ellps=intl +towgs84=114,-116,-333,0,0,0,0 +no_defs", 3, src_x, src_y, src_z,
        dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_6)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {10.0041319518545730, 10.0041219509666863, 10.0041419527424544};
    double dst_y[3] = {60.0008103785718205, 60.0008003792134943, 60.0008203779301681};
    run_single_case(
        "+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs",
        "+proj=longlat +a=6378249.2 +b=6356515 +towgs84=-73,-247,227,0,0,0,0 +no_defs", 3, src_x,
        src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_7)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {10.0034740976039771, 10.0034640952467591, 10.0034840999611987};
    double dst_y[3] = {60.0003619177622411, 60.0003519167560952, 60.0003719187683942};
    run_single_case(
        "+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs",
        "+proj=longlat +ellps=mod_airy +towgs84=482.5,-130.6,564.6,-1.042,-0.214,-0.631,8.15 "
        "+no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_11)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {9.9999999999999982, 9.9999900000000004, 10.0000099999999996};
    double dst_y[3] = {59.9999999999999929, 59.9999899999999968, 60.0000100000000103};
    run_single_case(
        "+proj=longlat +a=6378249.2 +b=6356515 +towgs84=-73,-247,227,0,0,0,0 +pm=paris +no_defs",
        "+proj=longlat +a=6378249.2 +b=6356515 +towgs84=-73,-247,227,0,0,0,0 +pm=paris +no_defs", 3,
        src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_12)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {12.3331844720841222, 12.3331744729167205, 12.3331944712515291};
    double dst_y[3] = {59.9992618147319163, 59.9992518140896962, 59.9992718153741365};
    run_single_case(
        "+proj=longlat +a=6378249.2 +b=6356515 +towgs84=-73,-247,227,0,0,0,0 +pm=paris +no_defs",
        "+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        1e-7);
}

TEST(latlong_world, generated_line_13)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {12.3356517464619628, 12.3356417462790926, 12.3356617466448366};
    double dst_y[3] = {60.0021406363754437, 60.0021306364510352, 60.0021506362998949};
    run_single_case(
        "+proj=longlat +a=6378249.2 +b=6356515 +towgs84=-73,-247,227,0,0,0,0 +pm=paris +no_defs",
        "+proj=longlat +ellps=intl +towgs84=114,-116,-333,0,0,0,0 +no_defs", 3, src_x, src_y, src_z,
        dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_14)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {12.3372291666666651, 12.3372191666666673, 12.3372391666666665};
    double dst_y[3] = {59.9999999999999929, 59.9999899999999968, 60.0000100000000103};
    run_single_case(
        "+proj=longlat +a=6378249.2 +b=6356515 +towgs84=-73,-247,227,0,0,0,0 +pm=paris +no_defs",
        "+proj=longlat +a=6378249.2 +b=6356515 +towgs84=-73,-247,227,0,0,0,0 +no_defs", 3, src_x,
        src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_15)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {12.3369853536327714, 12.3369753520342407, 12.3369953552313092};
    double dst_y[3] = {59.9995654862566141, 59.9995554846398989, 59.9995754878733436};
    run_single_case(
        "+proj=longlat +a=6378249.2 +b=6356515 +towgs84=-73,-247,227,0,0,0,0 +pm=paris +no_defs",
        "+proj=longlat +ellps=mod_airy +towgs84=482.5,-130.6,564.6,-1.042,-0.214,-0.631,8.15 "
        "+no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_19)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {10.0000000000000000, 9.9999900000000004, 10.0000099999999996};
    double dst_y[3] = {59.9999999999999929, 59.9999899999999968, 60.0000100000000103};
    run_single_case(
        "+proj=longlat +ellps=intl +towgs84=114,-116,-333,0,0,0,0 +no_defs",
        "+proj=longlat +ellps=intl +towgs84=114,-116,-333,0,0,0,0 +no_defs", 3, src_x, src_y, src_z,
        dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_20)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {9.9975981537176359, 9.9975881547306127, 9.9976081527046592};
    double dst_y[3] = {59.9970779907858400, 59.9970679900773121, 59.9970879914943893};
    run_single_case(
        "+proj=longlat +ellps=intl +towgs84=114,-116,-333,0,0,0,0 +no_defs",
        "+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        1e-7);
}

TEST(latlong_world, generated_line_21)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {7.6645006994424350, 7.6644906995675219, 7.6645106993173515};
    double dst_y[3] = {59.9978885481616402, 59.9978785480946968, 59.9978985482286120};
    run_single_case(
        "+proj=longlat +ellps=intl +towgs84=114,-116,-333,0,0,0,0 +no_defs",
        "+proj=longlat +a=6378249.2 +b=6356515 +towgs84=-73,-247,227,0,0,0,0 +pm=paris +no_defs", 3,
        src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_22)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {10.0017298661091036, 10.0017198662341880, 10.0017398659840193};
    double dst_y[3] = {59.9978885481616402, 59.9978785480946968, 59.9978985482286120};
    run_single_case(
        "+proj=longlat +ellps=intl +towgs84=114,-116,-333,0,0,0,0 +no_defs",
        "+proj=longlat +a=6378249.2 +b=6356515 +towgs84=-73,-247,227,0,0,0,0 +no_defs", 3, src_x,
        src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_23)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {10.0010716717194867, 10.0010616703756572, 10.0010816730633163};
    double dst_y[3] = {59.9974396056966768, 59.9974296039818711, 59.9974496074114896};
    run_single_case(
        "+proj=longlat +ellps=intl +towgs84=114,-116,-333,0,0,0,0 +no_defs",
        "+proj=longlat +ellps=mod_airy +towgs84=482.5,-130.6,564.6,-1.042,-0.214,-0.631,8.15 "
        "+no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_27)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {9.9982699250366895, 9.9982599249116682, 9.9982799251617127};
    double dst_y[3] = {60.0021113710984437, 60.0021013711653595, 60.0021213710315209};
    run_single_case(
        "+proj=longlat +a=6378249.2 +b=6356515 +towgs84=-73,-247,227,0,0,0,0 +no_defs",
        "+proj=longlat +ellps=intl +towgs84=114,-116,-333,0,0,0,0 +no_defs", 3, src_x, src_y, src_z,
        dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_28)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {9.9958679940704815, 9.9958579949585360, 9.9958779931824235};
    double dst_y[3] = {59.9991894641909553, 59.9991794635493250, 59.9991994648326212};
    run_single_case(
        "+proj=longlat +a=6378249.2 +b=6356515 +towgs84=-73,-247,227,0,0,0,0 +no_defs",
        "+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        1e-7);
}

TEST(latlong_world, generated_line_29)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {7.6627708333333340, 7.6627608333333326, 7.6627808333333318};
    double dst_y[3] = {59.9999999999999929, 59.9999899999999968, 60.0000100000000103};
    run_single_case(
        "+proj=longlat +a=6378249.2 +b=6356515 +towgs84=-73,-247,227,0,0,0,0 +no_defs",
        "+proj=longlat +a=6378249.2 +b=6356515 +towgs84=-73,-247,227,0,0,0,0 +pm=paris +no_defs", 3,
        src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_30)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {9.9982699250366895, 9.9982599249116682, 9.9982799251617127};
    double dst_y[3] = {60.0021113710984437, 60.0021013711653595, 60.0021213710315209};
    run_single_case(
        "+proj=longlat +a=6378249.2 +b=6356515 +towgs84=-73,-247,227,0,0,0,0 +no_defs",
        "+proj=longlat +ellps=intl +towgs84=114,-116,-333,0,0,0,0 +no_defs", 3, src_x, src_y, src_z,
        dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_31)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {9.9993414375626397, 9.9993314360937404, 9.9993514390315426};
    double dst_y[3] = {59.9995513796711180, 59.9995413780232099, 59.9995613813190687};
    run_single_case(
        "+proj=longlat +a=6378249.2 +b=6356515 +towgs84=-73,-247,227,0,0,0,0 +no_defs",
        "+proj=longlat +ellps=mod_airy +towgs84=482.5,-130.6,564.6,-1.042,-0.214,-0.631,8.15 "
        "+no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_35)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {10.0000000000000000, 9.9999900000000004, 10.0000099999999996};
    double dst_y[3] = {59.9999999999999929, 59.9999899999999968, 60.0000100000000103};
    run_single_case(
        "+proj=longlat +ellps=mod_airy +towgs84=482.5,-130.6,564.6,-1.042,-0.214,-0.631,8.15 "
        "+no_defs",
        "+proj=longlat +ellps=mod_airy +towgs84=482.5,-130.6,564.6,-1.042,-0.214,-0.631,8.15 "
        "+no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_36)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {9.9965264448194855, 9.9965164471761252, 9.9965364424628387};
    double dst_y[3] = {59.9996380475404365, 59.9996280485465334, 59.9996480465343396};
    run_single_case(
        "+proj=longlat +ellps=mod_airy +towgs84=482.5,-130.6,564.6,-1.042,-0.214,-0.631,8.15 "
        "+no_defs",
        "+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        1e-7);
}

TEST(latlong_world, generated_line_37)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {7.6634292905873522, 7.6634192920560835, 7.6634392891186183};
    double dst_y[3] = {60.0004485429341443, 60.0004385445817832, 60.0004585412865055};
    run_single_case(
        "+proj=longlat +ellps=mod_airy +towgs84=482.5,-130.6,564.6,-1.042,-0.214,-0.631,8.15 "
        "+no_defs",
        "+proj=longlat +a=6378249.2 +b=6356515 +towgs84=-73,-247,227,0,0,0,0 +pm=paris +no_defs", 3,
        src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_38)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {9.9989284122835809, 9.9989184136272851, 9.9989384109398696};
    double dst_y[3] = {60.0025599005786390, 60.0025499022931967, 60.0025698988640883};
    run_single_case(
        "+proj=longlat +ellps=mod_airy +towgs84=482.5,-130.6,564.6,-1.042,-0.214,-0.631,8.15 "
        "+no_defs",
        "+proj=longlat +ellps=intl +towgs84=114,-116,-333,0,0,0,0 +no_defs", 3, src_x, src_y, src_z,
        dst_x, dst_y, 1e-7);
}

TEST(latlong_world, generated_line_39)
{
    double src_x[3] = {10.000000, 9.999990, 10.000010};
    double src_y[3] = {60.000000, 59.999990, 60.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {10.0006584572540191, 10.0006484587227504, 10.0006684557852843};
    double dst_y[3] = {60.0004485429341443, 60.0004385445817832, 60.0004585412865055};
    run_single_case(
        "+proj=longlat +ellps=mod_airy +towgs84=482.5,-130.6,564.6,-1.042,-0.214,-0.631,8.15 "
        "+no_defs",
        "+proj=longlat +a=6378249.2 +b=6356515 +towgs84=-73,-247,227,0,0,0,0 +no_defs", 3, src_x,
        src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(lcc, generated_line_7)
{
    double src_x[3] = {-1.678505, -2.678505, -0.678505};
    double src_y[3] = {48.113850, 47.113850, 49.113850};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {352032.3065819768235087, 269790.7277162814280018, 431524.1866076080477796};
    double dst_y[3] = {
        6789586.9209155952557921, 6683652.0343577545136213, 6896703.2894891332834959
    };
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=lcc +lat_1=49 +lat_2=44 +lat_0=46.5 +lon_0=3 +x_0=700000 +y_0=6600000 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_8)
{
    double src_x[3] = {-1.678505, -2.678505, -0.678505};
    double src_y[3] = {48.113850, 47.113850, 49.113850};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {301123.0896497383364476, 219684.0690574069158174, 379787.6138205758761615};
    double dst_y[3] = {
        2353713.2962713576853275, 2247033.2415340621955693, 2461548.4547884692437947
    };
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_9)
{
    double src_x[3] = {-1.678505, -2.678505, -0.678505};
    double src_y[3] = {48.113850, 47.113850, 49.113850};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1351876.6194666770752519, 1269408.9724402632564306, 1431525.2406073887832463
    };
    double dst_y[3] = {
        7223223.9825555393472314, 7117345.4194151451811194, 7330264.6482031689956784
    };
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=lcc +lat_1=47.25 +lat_2=48.75 +lat_0=48 +lon_0=3 +x_0=1700000 +y_0=7200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_10)
{
    double src_x[3] = {-1.678505, -2.678505, -0.678505};
    double src_y[3] = {48.113850, 47.113850, 49.113850};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1351842.5395877079572529, 1269242.1726382232736796, 1431578.7104681753553450
    };
    double dst_y[3] = {
        8112195.0919896839186549, 8006374.6264312984421849, 8219164.7678996240720153
    };
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=lcc +lat_1=48.25 +lat_2=49.75 +lat_0=49 +lon_0=3 +x_0=1700000 +y_0=8200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_14)
{
    double src_x[3] = {342634.000000, 342633.999990, 342634.000010};
    double src_y[3] = {6786169.000000, 6786168.999990, 6786169.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {-1.8017688863346495, -1.8017688864605248, -1.8017688862087751};
    double dst_y[3] = {48.0780752825984194, 48.0780752825031357, 48.0780752826937032};
    run_single_case(
        "+proj=lcc +lat_1=49 +lat_2=44 +lat_0=46.5 +lon_0=3 +x_0=700000 +y_0=6600000 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(lcc, generated_line_18)
{
    double src_x[3] = {300257.000000, 300256.999990, 300257.000010};
    double src_y[3] = {2356309.000000, 2356308.999990, 2356309.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {-1.6919053934062167, -1.6919053935334942, -1.6919053932789399};
    double dst_y[3] = {48.1367616458734062, 48.1367616457789893, 48.1367616459678160};
    run_single_case(
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(lcc, generated_line_22)
{
    double src_x[3] = {1353238.000000, 1353237.999990, 1353238.000010};
    double src_y[3] = {7221609.000000, 7221608.999990, 7221609.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {-1.6589437454082179, -1.6589437455341391, -1.6589437452822966};
    double dst_y[3] = {48.1000922238543609, 48.1000922237591482, 48.1000922239495736};
    run_single_case(
        "+proj=lcc +lat_1=47.25 +lat_2=48.75 +lat_0=48 +lon_0=3 +x_0=1700000 +y_0=7200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(lcc, generated_line_26)
{
    double src_x[3] = {1350954.000000, 1350953.999990, 1350954.000010};
    double src_y[3] = {8113986.000000, 8113985.999990, 8113986.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {-1.6919002636079667, -1.6919002637337490, -1.6919002634821829};
    double dst_y[3] = {48.1294323320347743, 48.1294323319394621, 48.1294323321300794};
    run_single_case(
        "+proj=lcc +lat_1=48.25 +lat_2=49.75 +lat_0=49 +lon_0=3 +x_0=1700000 +y_0=8200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(lcc, generated_line_30)
{
    double src_x[3] = {342634.000000, 342633.000000, 342635.000000};
    double src_y[3] = {6786169.000000, 6786168.000000, 6786170.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {342633.9999998952844180, 342632.9999998952844180, 342634.9999998952844180};
    double dst_y[3] = {
        6786168.9999982798472047, 6786167.9999982789158821, 6786169.9999982789158821
    };
    run_single_case(
        "+proj=lcc +lat_1=49 +lat_2=44 +lat_0=46.5 +lon_0=3 +x_0=700000 +y_0=6600000 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=lcc +lat_1=49 +lat_2=44 +lat_0=46.5 +lon_0=3 +x_0=700000 +y_0=6600000 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_31)
{
    double src_x[3] = {342634.000000, 342633.000000, 342635.000000};
    double src_y[3] = {6786169.000000, 6786168.000000, 6786170.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {291746.3098040802287869, 291745.3172816481674090, 291747.3023265108349733};
    double dst_y[3] = {
        2350216.3729945542290807, 2350215.3641597288660705, 2350217.3818293847143650
    };
    run_single_case(
        "+proj=lcc +lat_1=49 +lat_2=44 +lat_0=46.5 +lon_0=3 +x_0=700000 +y_0=6600000 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_32)
{
    double src_x[3] = {342634.000000, 342633.000000, 342635.000000};
    double src_y[3] = {6786169.000000, 6786168.000000, 6786170.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {589247.0154859493486583, 589246.0944459831807762, 589247.9365259062033147};
    double dst_y[3] = {
        5325672.4393173353746533, 5325671.3656392972916365, 5325673.5129953743889928
    };
    run_single_case(
        "+proj=lcc +lat_1=49 +lat_2=44 +lat_0=46.5 +lon_0=3 +x_0=700000 +y_0=6600000 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=utm +zone=30 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        0.01);
}

TEST(lcc, generated_line_33)
{
    double src_x[3] = {342634.000000, 342633.000000, 342635.000000};
    double src_y[3] = {6786169.000000, 6786168.000000, 6786170.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1342468.8039805821608752, 1342467.8020169432274997, 1342469.8059442131780088
    };
    double dst_y[3] = {
        7219818.0689767086878419, 7219817.0699575673788786, 7219819.0679958490654826
    };
    run_single_case(
        "+proj=lcc +lat_1=49 +lat_2=44 +lat_0=46.5 +lon_0=3 +x_0=700000 +y_0=6600000 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=lcc +lat_1=47.25 +lat_2=48.75 +lat_0=48 +lon_0=3 +x_0=1700000 +y_0=7200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_34)
{
    double src_x[3] = {342634.000000, 342633.000000, 342635.000000};
    double src_y[3] = {6786169.000000, 6786168.000000, 6786170.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1342430.3164079277776182, 1342429.3133481671102345, 1342431.3194676749408245
    };
    double dst_y[3] = {
        8108797.7650425340980291, 8108796.7668673116713762, 8108798.7632177555933595
    };
    run_single_case(
        "+proj=lcc +lat_1=49 +lat_2=44 +lat_0=46.5 +lon_0=3 +x_0=700000 +y_0=6600000 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=lcc +lat_1=48.25 +lat_2=49.75 +lat_0=49 +lon_0=3 +x_0=1700000 +y_0=8200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_38)
{
    double src_x[3] = {300257.000000, 300256.000000, 300258.000000};
    double src_y[3] = {2356309.000000, 2356308.000000, 2356310.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {351188.0174407793674618, 351187.0100291651906446, 351189.0248523951740935};
    double dst_y[3] = {
        6792187.7626350661739707, 6792186.7715255916118622, 6792188.7537445425987244
    };
    run_single_case(
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        "+proj=lcc +lat_1=49 +lat_2=44 +lat_0=46.5 +lon_0=3 +x_0=700000 +y_0=6600000 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_39)
{
    double src_x[3] = {300257.000000, 300256.000000, 300258.000000};
    double src_y[3] = {2356309.000000, 2356308.000000, 2356310.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {300256.9999999050633051, 300255.9999999051215127, 300257.9999999050050974};
    double dst_y[3] = {
        2356308.9999981494620442, 2356307.9999981499277055, 2356309.9999981489963830
    };
    run_single_case(
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_40)
{
    double src_x[3] = {300257.000000, 300256.000000, 300258.000000};
    double src_y[3] = {2356309.000000, 2356308.000000, 2356310.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {597318.8847237481968477, 597317.9556773778749630, 597319.8137701104860753};
    double dst_y[3] = {
        5332328.3115261234343052, 5332327.2461127247661352, 5332329.3769395258277655
    };
    run_single_case(
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        "+proj=utm +zone=30 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        0.01);
}

TEST(lcc, generated_line_41)
{
    double src_x[3] = {300257.000000, 300256.000000, 300258.000000};
    double src_y[3] = {2356309.000000, 2356308.000000, 2356310.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1351035.6697100149467587, 1351034.6604046479333192, 1351036.6790153754409403
    };
    double dst_y[3] = {
        7225827.2590050818398595, 7225826.2688843514770269, 7225828.2491258140653372
    };
    run_single_case(
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        "+proj=lcc +lat_1=47.25 +lat_2=48.75 +lat_0=48 +lon_0=3 +x_0=1700000 +y_0=7200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_42)
{
    double src_x[3] = {300257.000000, 300256.000000, 300258.000000};
    double src_y[3] = {2356309.000000, 2356308.000000, 2356310.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1351003.9567958230618387, 1351002.9464418329298496, 1351004.9671498013194650
    };
    double dst_y[3] = {
        8114799.4586451435461640, 8114798.4693716755136847, 8114800.4479186143726110
    };
    run_single_case(
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        "+proj=lcc +lat_1=48.25 +lat_2=49.75 +lat_0=49 +lon_0=3 +x_0=1700000 +y_0=8200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_46)
{
    double src_x[3] = {599010.000000, 599009.000000, 599011.000000};
    double src_y[3] = {5329096.000000, 5329095.000000, 5329097.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {352627.0188855871092528, 352625.9457595213316381, 352628.0920116612687707};
    double dst_y[3] = {
        6788836.6882963748648763, 6788835.7678244765847921, 6788837.6087682684883475
    };
    run_single_case(
        "+proj=utm +zone=30 +datum=WGS84 +units=m +no_defs",
        "+proj=lcc +lat_1=49 +lat_2=44 +lat_0=46.5 +lon_0=3 +x_0=700000 +y_0=6600000 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_47)
{
    double src_x[3] = {599010.000000, 599009.000000, 599011.000000};
    double src_y[3] = {5329096.000000, 5329095.000000, 5329097.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {301724.3284255259204656, 301723.2620887027587742, 301725.3947623560670763};
    double dst_y[3] = {
        2352967.4117347463034093, 2352966.4818797251209617, 2352968.3415897632949054
    };
    run_single_case(
        "+proj=utm +zone=30 +datum=WGS84 +units=m +no_defs",
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_48)
{
    double src_x[3] = {599010.000000, 599009.000000, 599011.000000};
    double src_y[3] = {5329096.000000, 5329095.000000, 5329097.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {599010.0000000000000000, 599009.0000000000000000, 599011.0000000000000000};
    double dst_y[3] = {
        5329096.0000000000000000, 5329095.0000000009313226, 5329096.9999999990686774
    };
    run_single_case(
        "+proj=utm +zone=30 +datum=WGS84 +units=m +no_defs",
        "+proj=utm +zone=30 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        0.01);
}

TEST(lcc, generated_line_49)
{
    double src_x[3] = {599010.000000, 599009.000000, 599011.000000};
    double src_y[3] = {5329096.000000, 5329095.000000, 5329097.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1352470.5400032349862158, 1352469.4650462595745921, 1352471.6149602108635008
    };
    double dst_y[3] = {
        7222472.5401266179978848, 7222471.6207512309774756, 7222473.4595020022243261
    };
    run_single_case(
        "+proj=utm +zone=30 +datum=WGS84 +units=m +no_defs",
        "+proj=lcc +lat_1=47.25 +lat_2=48.75 +lat_0=48 +lon_0=3 +x_0=1700000 +y_0=7200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_50)
{
    double src_x[3] = {599010.000000, 599009.000000, 599011.000000};
    double src_y[3] = {5329096.000000, 5329095.000000, 5329097.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1352435.8209565244615078, 1352434.7450052832718939, 1352436.8969077605288476
    };
    double dst_y[3] = {
        8111443.0007124813273549, 8111442.0822414122521877, 8111443.9191835494711995
    };
    run_single_case(
        "+proj=utm +zone=30 +datum=WGS84 +units=m +no_defs",
        "+proj=lcc +lat_1=48.25 +lat_2=49.75 +lat_0=49 +lon_0=3 +x_0=1700000 +y_0=8200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_54)
{
    double src_x[3] = {1353238.000000, 1353237.000000, 1353239.000000};
    double src_y[3] = {7221609.000000, 7221608.000000, 7221610.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {353395.3431278624339029, 353394.3450381130678579, 353396.3412176202400587};
    double dst_y[3] = {
        6787974.6604768643155694, 6787973.6595329828560352, 6787975.6614207485690713
    };
    run_single_case(
        "+proj=lcc +lat_1=47.25 +lat_2=48.75 +lat_0=48 +lon_0=3 +x_0=1700000 +y_0=7200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=lcc +lat_1=49 +lat_2=44 +lat_0=46.5 +lon_0=3 +x_0=700000 +y_0=6600000 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_55)
{
    double src_x[3] = {1353238.000000, 1353237.000000, 1353239.000000};
    double src_y[3] = {7221609.000000, 7221608.000000, 7221610.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {302500.2100574134383351, 302499.2194646109128371, 302501.2006502227159217};
    double dst_y[3] = {
        2352111.0735905305482447, 2352110.0638204286806285, 2352112.0833606352098286
    };
    run_single_case(
        "+proj=lcc +lat_1=47.25 +lat_2=48.75 +lat_0=48 +lon_0=3 +x_0=1700000 +y_0=7200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_56)
{
    double src_x[3] = {1353238.000000, 1353237.000000, 1353239.000000};
    double src_y[3] = {7221609.000000, 7221608.000000, 7221610.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {599842.1348465713672340, 599841.2158273181412369, 599843.0538658233126625};
    double dst_y[3] = {
        5328294.9205505279824138, 5328293.8460137173533440, 5328295.9950873432680964
    };
    run_single_case(
        "+proj=lcc +lat_1=47.25 +lat_2=48.75 +lat_0=48 +lon_0=3 +x_0=1700000 +y_0=7200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=utm +zone=30 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        0.01);
}

TEST(lcc, generated_line_57)
{
    double src_x[3] = {1353238.000000, 1353237.000000, 1353239.000000};
    double src_y[3] = {7221609.000000, 7221608.000000, 7221610.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1353237.9999998961575329, 1353236.9999998963903636, 1353238.9999998961575329
    };
    double dst_y[3] = {
        7221608.9999982845038176, 7221607.9999982845038176, 7221609.9999982845038176
    };
    run_single_case(
        "+proj=lcc +lat_1=47.25 +lat_2=48.75 +lat_0=48 +lon_0=3 +x_0=1700000 +y_0=7200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=lcc +lat_1=47.25 +lat_2=48.75 +lat_0=48 +lon_0=3 +x_0=1700000 +y_0=7200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_58)
{
    double src_x[3] = {1353238.000000, 1353237.000000, 1353239.000000};
    double src_y[3] = {7221609.000000, 7221608.000000, 7221610.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1353202.5597280114889145, 1353201.5586666348390281, 1353203.5607893825508654
    };
    double dst_y[3] = {
        8110578.6345151709392667, 8110577.6353348297998309, 8110579.6336955130100250
    };
    run_single_case(
        "+proj=lcc +lat_1=47.25 +lat_2=48.75 +lat_0=48 +lon_0=3 +x_0=1700000 +y_0=7200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=lcc +lat_1=48.25 +lat_2=49.75 +lat_0=49 +lon_0=3 +x_0=1700000 +y_0=8200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_62)
{
    double src_x[3] = {1350954.000000, 1350953.000000, 1350955.000000};
    double src_y[3] = {8113986.000000, 8113985.000000, 8113987.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {351140.0285800683195703, 351139.0315475688548759, 351141.0256125815212727};
    double dst_y[3] = {
        6791374.6576918447390199, 6791373.6558923004195094, 6791375.6594913899898529
    };
    run_single_case(
        "+proj=lcc +lat_1=48.25 +lat_2=49.75 +lat_0=49 +lon_0=3 +x_0=1700000 +y_0=8200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=lcc +lat_1=49 +lat_2=44 +lat_0=46.5 +lon_0=3 +x_0=700000 +y_0=6600000 +ellps=GRS80 "
        "+towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_63)
{
    double src_x[3] = {1350954.000000, 1350953.000000, 1350955.000000};
    double src_y[3] = {8113986.000000, 8113985.000000, 8113987.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {300215.6163186620688066, 300214.6267913912888616, 300216.6058459450723603};
    double dst_y[3] = {
        2355494.9555618148297071, 2355493.9449486187659204, 2355495.9661750150844455
    };
    run_single_case(
        "+proj=lcc +lat_1=48.25 +lat_2=49.75 +lat_0=49 +lon_0=3 +x_0=1700000 +y_0=8200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_64)
{
    double src_x[3] = {1350954.000000, 1350953.000000, 1350955.000000};
    double src_y[3] = {8113986.000000, 8113985.000000, 8113987.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {597333.1188605014467612, 597332.2009766089031473, 597334.0367443979484960};
    double dst_y[3] = {
        5331513.6991804270073771, 5331512.6238958174362779, 5331514.7744650403037667
    };
    run_single_case(
        "+proj=lcc +lat_1=48.25 +lat_2=49.75 +lat_0=49 +lon_0=3 +x_0=1700000 +y_0=8200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=utm +zone=30 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        0.01);
}

TEST(lcc, generated_line_65)
{
    double src_x[3] = {1350954.000000, 1350953.000000, 1350955.000000};
    double src_y[3] = {8113986.000000, 8113985.000000, 8113987.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1350986.4888023838866502, 1350985.4898622252512723, 1350987.4877425478771329
    };
    double dst_y[3] = {
        7225013.8439853414893150, 7225012.8431512853130698, 7225014.8448193995282054
    };
    run_single_case(
        "+proj=lcc +lat_1=48.25 +lat_2=49.75 +lat_0=49 +lon_0=3 +x_0=1700000 +y_0=8200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=lcc +lat_1=47.25 +lat_2=48.75 +lat_0=48 +lon_0=3 +x_0=1700000 +y_0=7200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_66)
{
    double src_x[3] = {1350954.000000, 1350953.000000, 1350955.000000};
    double src_y[3] = {8113986.000000, 8113985.000000, 8113987.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1350953.9999998942948878, 1350952.9999998942948878, 1350954.9999998945277184
    };
    double dst_y[3] = {
        8113985.9999982938170433, 8113984.9999982928857207, 8113986.9999982947483659
    };
    run_single_case(
        "+proj=lcc +lat_1=48.25 +lat_2=49.75 +lat_0=49 +lon_0=3 +x_0=1700000 +y_0=8200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=lcc +lat_1=48.25 +lat_2=49.75 +lat_0=49 +lon_0=3 +x_0=1700000 +y_0=8200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_74)
{
    double src_x[3] = {7.420449, 6.420449, 8.420449};
    double src_y[3] = {43.731709, 42.731709, 44.731709};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {1009833.4125478556379676, 935079.8746634233975783, 1081700.8960147500038147};
    double dst_y[3] = {
        1872140.0173203139565885, 1756292.6232172241434455, 1988747.4813167566899210
    };
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_75)
{
    double src_x[3] = {7.420449, 6.420449, 8.420449};
    double src_y[3] = {43.731709, 42.731709, 44.731709};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        2055944.1860781735740602, 1980080.9986208970658481, 2129064.2698346409015357
    };
    double dst_y[3] = {
        3179733.4940027915872633, 3064902.0649659642949700, 3295406.4027706249617040
    };
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=lcc +lat_1=43.25 +lat_2=44.75 +lat_0=44 +lon_0=3 +x_0=1700000 +y_0=3200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_76)
{
    double src_x[3] = {7.420449, 6.420449, 8.420449};
    double src_y[3] = {43.731709, 42.731709, 44.731709};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        2055975.3343498106114566, 1980018.9275816893205047, 2129237.4535399544984102
    };
    double dst_y[3] = {
        2290655.6599310589954257, 2175899.2524127978831530, 2406274.9034834904596210
    };
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=lcc +lat_1=42.25 +lat_2=43.75 +lat_0=43 +lon_0=3 +x_0=1700000 +y_0=2200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_80)
{
    double src_x[3] = {1009631.000000, 1009630.999990, 1009631.000010};
    double src_y[3] = {1871858.000000, 1871857.999990, 1871858.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {7.4177193014455964, 7.4177193013138911, 7.4177193015772982};
    double dst_y[3] = {43.7292968707014893, 43.7292968706175813, 43.7292968707853760};
    run_single_case(
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(lcc, generated_line_84)
{
    double src_x[3] = {2055952.000000, 2055951.999990, 2055952.000010};
    double src_y[3] = {3180022.000000, 3180021.999990, 3180022.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {7.4207377126433069, 7.4207377125126976, 7.4207377127739154};
    double dst_y[3] = {43.7342983457881260, 43.7342983457030599, 43.7342983458731851};
    run_single_case(
        "+proj=lcc +lat_1=43.25 +lat_2=44.75 +lat_0=44 +lon_0=3 +x_0=1700000 +y_0=3200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(lcc, generated_line_88)
{
    double src_x[3] = {2055984.000000, 2055983.999990, 2055984.000010};
    double src_y[3] = {2290944.000000, 2290943.999990, 2290944.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {7.4207446534303454, 7.4207446532998622, 7.4207446535608295};
    double dst_y[3] = {43.7342964716644289, 43.7342964715792846, 43.7342964717495661};
    run_single_case(
        "+proj=lcc +lat_1=42.25 +lat_2=43.75 +lat_0=43 +lon_0=3 +x_0=1700000 +y_0=2200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(lcc, generated_line_92)
{
    double src_x[3] = {1009631.000000, 1009630.000000, 1009632.000000};
    double src_y[3] = {1871858.000000, 1871857.000000, 1871859.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {372565.9483037012396380, 372564.8697818435030058, 372567.0268255735281855};
    double dst_y[3] = {
        4843023.6483989944681525, 4843022.7368946680799127, 4843024.5599033078178763
    };
    run_single_case(
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        "+proj=utm +zone=32 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        0.01);
}

TEST(lcc, generated_line_93)
{
    double src_x[3] = {1009631.000000, 1009630.000000, 1009632.000000};
    double src_y[3] = {1871858.000000, 1871857.000000, 1871859.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        2055738.9421076159924269, 2055737.9324703884776682, 2055739.9517448577098548
    };
    double dst_y[3] = {
        3179454.1172142555005848, 3179453.1296995515003800, 3179455.1047289534471929
    };
    run_single_case(
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        "+proj=lcc +lat_1=43.25 +lat_2=44.75 +lat_0=44 +lon_0=3 +x_0=1700000 +y_0=3200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_94)
{
    double src_x[3] = {1009631.000000, 1009630.000000, 1009632.000000};
    double src_y[3] = {1871858.000000, 1871857.000000, 1871859.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        2055769.8033214854076505, 2055768.7926505531650037, 2055770.8139924376737326
    };
    double dst_y[3] = {
        2290376.4641896486282349, 2290375.4775924403220415, 2290377.4507868499495089
    };
    run_single_case(
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        "+proj=lcc +lat_1=42.25 +lat_2=43.75 +lat_0=43 +lon_0=3 +x_0=1700000 +y_0=2200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_95)
{
    double src_x[3] = {1009631.000000, 1009630.000000, 1009632.000000};
    double src_y[3] = {1871858.000000, 1871857.000000, 1871859.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1009631.0000000006984919, 1009630.0000000009313226, 1009632.0000000006984919
    };
    double dst_y[3] = {
        1871857.9999999881256372, 1871856.9999999864958227, 1871858.9999999885912985
    };
    run_single_case(
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_99)
{
    double src_x[3] = {372820.000000, 372819.000000, 372821.000000};
    double src_y[3] = {4843574.000000, 4843573.000000, 4843575.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {372820.0000000000000000, 372819.0000000000000000, 372821.0000000000582077};
    double dst_y[3] = {
        4843573.9999999990686774, 4843572.9999999981373549, 4843574.9999999990686774
    };
    run_single_case(
        "+proj=utm +zone=32 +datum=WGS84 +units=m +no_defs",
        "+proj=utm +zone=32 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        0.01);
}

TEST(lcc, generated_line_100)
{
    double src_x[3] = {372820.000000, 372819.000000, 372821.000000};
    double src_y[3] = {4843574.000000, 4843573.000000, 4843575.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        2055952.3996505602262914, 2055951.4747643556911498, 2055953.3245367628987879
    };
    double dst_y[3] = {
        3180021.5304268207401037, 3180020.4603372653946280, 3180022.6005163774825633
    };
    run_single_case(
        "+proj=utm +zone=32 +datum=WGS84 +units=m +no_defs",
        "+proj=lcc +lat_1=43.25 +lat_2=44.75 +lat_0=44 +lon_0=3 +x_0=1700000 +y_0=3200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_101)
{
    double src_x[3] = {372820.000000, 372819.000000, 372821.000000};
    double src_y[3] = {4843574.000000, 4843573.000000, 4843575.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        2055983.8298712631221861, 2055982.9038744654972106, 2055984.7558680647052824
    };
    double dst_y[3] = {
        2290943.7085315203294158, 2290942.6392700183205307, 2290944.7777930237352848
    };
    run_single_case(
        "+proj=utm +zone=32 +datum=WGS84 +units=m +no_defs",
        "+proj=lcc +lat_1=42.25 +lat_2=43.75 +lat_0=43 +lon_0=3 +x_0=1700000 +y_0=2200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_102)
{
    double src_x[3] = {372820.000000, 372819.000000, 372821.000000};
    double src_y[3] = {4843574.000000, 4843573.000000, 4843575.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1009838.4415659843944013, 1009837.5273490578401834, 1009839.3557828948833048
    };
    double dst_y[3] = {
        1872428.5193446264602244, 1872427.4376049803104252, 1872429.6010842705145478
    };
    run_single_case(
        "+proj=utm +zone=32 +datum=WGS84 +units=m +no_defs",
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_106)
{
    double src_x[3] = {2055952.000000, 2055951.000000, 2055953.000000};
    double src_y[3] = {3180022.000000, 3180021.000000, 3180023.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {372819.6355374456034042, 372818.5657186300959438, 372820.7053562616929412};
    double dst_y[3] = {
        4843574.4972829967737198, 4843573.5726307937875390, 4843575.4219351923093200
    };
    run_single_case(
        "+proj=lcc +lat_1=43.25 +lat_2=44.75 +lat_0=44 +lon_0=3 +x_0=1700000 +y_0=3200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=utm +zone=32 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        0.01);
}

TEST(lcc, generated_line_107)
{
    double src_x[3] = {2055952.000000, 2055951.000000, 2055953.000000};
    double src_y[3] = {3180022.000000, 3180021.000000, 3180023.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        2055952.0000000004656613, 2055951.0000000006984919, 2055953.0000000006984919
    };
    double dst_y[3] = {
        3180021.9999999888241291, 3180020.9999999888241291, 3180022.9999999874271452
    };
    run_single_case(
        "+proj=lcc +lat_1=43.25 +lat_2=44.75 +lat_0=44 +lon_0=3 +x_0=1700000 +y_0=3200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=lcc +lat_1=43.25 +lat_2=44.75 +lat_0=44 +lon_0=3 +x_0=1700000 +y_0=3200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_108)
{
    double src_x[3] = {2055952.000000, 2055951.000000, 2055953.000000};
    double src_y[3] = {3180022.000000, 3180021.000000, 3180023.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        2055983.4306512437760830, 2055982.4296038234606385, 2055984.4316986696794629
    };
    double dst_y[3] = {
        2290944.1785282236523926, 2290943.1794345974922180, 2290945.1776218498125672
    };
    run_single_case(
        "+proj=lcc +lat_1=43.25 +lat_2=44.75 +lat_0=44 +lon_0=3 +x_0=1700000 +y_0=3200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=lcc +lat_1=42.25 +lat_2=43.75 +lat_0=43 +lon_0=3 +x_0=1700000 +y_0=2200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_109)
{
    double src_x[3] = {2055952.000000, 2055951.000000, 2055953.000000};
    double src_y[3] = {3180022.000000, 3180021.000000, 3180023.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1009838.0361939561553299, 1009837.0459881389979273, 1009839.0263997586444020
    };
    double dst_y[3] = {
        1872428.9849897632375360, 1872427.9725974751636386, 1872429.9973820499144495
    };
    run_single_case(
        "+proj=lcc +lat_1=43.25 +lat_2=44.75 +lat_0=44 +lon_0=3 +x_0=1700000 +y_0=3200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_113)
{
    double src_x[3] = {2055984.000000, 2055983.000000, 2055985.000000};
    double src_y[3] = {2290944.000000, 2290943.000000, 2290945.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {372820.1905286786495708, 372819.1216894905664958, 372821.2593678617849946};
    double dst_y[3] = {
        4843574.2784806881099939, 4843573.3528496194630861, 4843575.2041117511689663
    };
    run_single_case(
        "+proj=lcc +lat_1=42.25 +lat_2=43.75 +lat_0=43 +lon_0=3 +x_0=1700000 +y_0=2200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=utm +zone=32 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        0.01);
}

TEST(lcc, generated_line_114)
{
    double src_x[3] = {2055984.000000, 2055983.000000, 2055985.000000};
    double src_y[3] = {2290944.000000, 2290943.000000, 2290945.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        2055952.5694824429228902, 2055951.5705306748859584, 2055953.5684342058375478
    };
    double dst_y[3] = {
        3180021.8220406319014728, 3180020.8211353458464146, 3180022.8229459193535149
    };
    run_single_case(
        "+proj=lcc +lat_1=42.25 +lat_2=43.75 +lat_0=43 +lon_0=3 +x_0=1700000 +y_0=2200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=lcc +lat_1=43.25 +lat_2=44.75 +lat_0=44 +lon_0=3 +x_0=1700000 +y_0=3200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_115)
{
    double src_x[3] = {2055984.000000, 2055983.000000, 2055985.000000};
    double src_y[3] = {2290944.000000, 2290943.000000, 2290945.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        2055984.0000000004656613, 2055983.0000000004656613, 2055985.0000000006984919
    };
    double dst_y[3] = {
        2290943.9999999906867743, 2290942.9999999892897904, 2290944.9999999878928065
    };
    run_single_case(
        "+proj=lcc +lat_1=42.25 +lat_2=43.75 +lat_0=43 +lon_0=3 +x_0=1700000 +y_0=2200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=lcc +lat_1=42.25 +lat_2=43.75 +lat_0=43 +lon_0=3 +x_0=1700000 +y_0=2200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_116)
{
    double src_x[3] = {2055984.000000, 2055983.000000, 2055985.000000};
    double src_y[3] = {2290944.000000, 2290943.000000, 2290945.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1009838.6083903226535767, 1009837.6192441440653056, 1009839.5975364809855819
    };
    double dst_y[3] = {
        1872428.8131166244857013, 1872427.7998294983990490, 1872429.8264037517365068
    };
    run_single_case(
        "+proj=lcc +lat_1=42.25 +lat_2=43.75 +lat_0=43 +lon_0=3 +x_0=1700000 +y_0=2200000 "
        "+ellps=GRS80 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs",
        "+proj=lcc +lat_1=46.8 +lat_0=46.8 +lon_0=0 +k_0=0.99987742 +x_0=600000 +y_0=2200000 "
        "+a=6378249.2 +b=6356515 +towgs84=-168,-60,320,0,0,0,0 +pm=paris +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(lcc, generated_line_124)
{
    double src_x[3] = {2012069.000000, 2012068.999990, 2012069.000010};
    double src_y[3] = {465650.000000, 465649.999990, 465650.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {-80.8482173785606619, -80.8482173785955496, -80.8482173785257743};
    double dst_y[3] = {38.2786845797926176, 38.2786845797652049, 38.2786845798200375};
    run_single_case(
        "+proj=lcc +lat_1=38.88333333333333 +lat_2=37.48333333333333 +lat_0=37 +lon_0=-81 "
        "+x_0=600000 +y_0=0 +datum=NAD83 +units=us-ft +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(lcc, generated_line_128)
{
    double src_x[3] = {-80.848217, -81.848217, -79.848217};
    double src_y[3] = {38.278685, 37.278685, 39.278685};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        2012069.1084146420471370, 1721691.2664955426007509, 2294583.4600928905420005
    };
    double dst_y[3] = {465650.1531968949711882, 102610.0387049150303937, 831847.2608613269403577};
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=lcc +lat_1=38.88333333333333 +lat_2=37.48333333333333 +lat_0=37 +lon_0=-81 "
        "+x_0=600000 +y_0=0 +datum=NAD83 +units=us-ft +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_3)
{
    double src_x[3] = {-122.000000, -123.000000, -121.000000};
    double src_y[3] = {47.000000, 46.000000, 48.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        -13580977.8767793755978346, -13692297.3675726503133774, -13469658.3859861027449369
    };
    double dst_y[3] = {
        5910809.6197671229019761, 5749599.5463615944609046, 6075085.0900578452274203
    };
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=merc +lon_0=0 +k=1 +x_0=0 +y_0=0 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y,
        src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_4)
{
    double src_x[3] = {-122.000000, -123.000000, -121.000000};
    double src_y[3] = {47.000000, 46.000000, 48.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        9796115.1898080762475729, 9684795.6990147978067398, 9907434.6806013435125351
    };
    double dst_y[3] = {
        5910809.6197671229019761, 5749599.5463615944609046, 6075085.0900578452274203
    };
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=merc +lon_0=150 +k=1 +x_0=0 +y_0=0 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y,
        src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_5)
{
    double src_x[3] = {-122.000000, -123.000000, -121.000000};
    double src_y[3] = {47.000000, 46.000000, 48.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        -13580977.8767793755978346, -13692297.3675726503133774, -13469658.3859861027449369
    };
    double dst_y[3] = {
        5942074.0724311079829931, 5780349.2202563509345055, 6106854.8348850747570395
    };
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_9)
{
    double src_x[3] = {-13614351.000000, -13614351.000010, -13614350.999990};
    double src_y[3] = {600256.000000, 600255.999990, 600256.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {-122.2997958666788918, -122.2997958667687328, -122.2997958665890792};
    double dst_y[3] = {5.4203205929355551, 5.4203205928455196, 5.4203205930255773};
    run_single_case(
        "+proj=merc +lon_0=0 +k=1 +x_0=0 +y_0=0 +datum=WGS84 +units=m +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(merc, generated_line_13)
{
    double src_x[3] = {9762741.000000, 9762740.999990, 9762741.000010};
    double src_y[3] = {6000256.000000, 6000255.999990, 6000256.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {-122.2998054479970023, -122.2998054480868149, -122.2998054479071328};
    double dst_y[3] = {47.5468666846378269, 47.5468666845770045, 47.5468666846986423};
    run_single_case(
        "+proj=merc +lon_0=150 +k=1 +x_0=0 +y_0=0 +datum=WGS84 +units=m +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(merc, generated_line_17)
{
    double src_x[3] = {-13614351.981900, -13614351.981910, -13614351.981890};
    double src_y[3] = {6031798.776040, 6031798.776030, 6031798.776050};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {-122.2998046872366587, -122.2998046873264997, -122.2998046871468460};
    double dst_y[3] = {47.5468715989234170, 47.5468715988627793, 47.5468715989840476};
    run_single_case(
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(merc, generated_line_21)
{
    double src_x[3] = {-13614351.000000, -13614352.000000, -13614350.000000};
    double src_y[3] = {600256.000000, 600255.000000, 600257.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        -13614350.9999999981373549, -13614351.9999999981373549, -13614349.9999999981373549
    };
    double dst_y[3] = {600255.9999999488936737, 600254.9999999491265044, 600256.9999999484280124};
    run_single_case(
        "+proj=merc +lon_0=0 +k=1 +x_0=0 +y_0=0 +datum=WGS84 +units=m +no_defs",
        "+proj=merc +lon_0=0 +k=1 +x_0=0 +y_0=0 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y,
        src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_22)
{
    double src_x[3] = {-13614351.000000, -13614352.000000, -13614350.000000};
    double src_y[3] = {600256.000000, 600255.000000, 600257.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        9762742.0665874499827623, 9762741.0665874499827623, 9762743.0665874499827623
    };
    double dst_y[3] = {600255.9999999488936737, 600254.9999999491265044, 600256.9999999484280124};
    run_single_case(
        "+proj=merc +lon_0=0 +k=1 +x_0=0 +y_0=0 +datum=WGS84 +units=m +no_defs",
        "+proj=merc +lon_0=150 +k=1 +x_0=0 +y_0=0 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y,
        src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_23)
{
    double src_x[3] = {-13614351.000000, -13614352.000000, -13614350.000000};
    double src_y[3] = {600256.000000, 600255.000000, 600257.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        -13614350.9999999981373549, -13614351.9999999981373549, -13614349.9999999981373549
    };
    double dst_y[3] = {604289.3620224298210815, 604288.3553430694155395, 604290.3687017909251153};
    run_single_case(
        "+proj=merc +lon_0=0 +k=1 +x_0=0 +y_0=0 +datum=WGS84 +units=m +no_defs",
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_27)
{
    double src_x[3] = {9762741.000000, 9762740.000000, 9762742.000000};
    double src_y[3] = {6000256.000000, 6000255.000000, 6000257.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        -13614352.0665874499827623, -13614353.0665874499827623, -13614351.0665874499827623
    };
    double dst_y[3] = {
        6000255.9999972283840179, 6000254.9999972293153405, 6000256.9999972283840179
    };
    run_single_case(
        "+proj=merc +lon_0=150 +k=1 +x_0=0 +y_0=0 +datum=WGS84 +units=m +no_defs",
        "+proj=merc +lon_0=0 +k=1 +x_0=0 +y_0=0 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y,
        src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_28)
{
    double src_x[3] = {9762741.000000, 9762740.000000, 9762742.000000};
    double src_y[3] = {6000256.000000, 6000255.000000, 6000257.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        9762740.9999999944120646, 9762739.9999999944120646, 9762741.9999999944120646
    };
    double dst_y[3] = {
        6000255.9999972283840179, 6000254.9999972293153405, 6000256.9999972283840179
    };
    run_single_case(
        "+proj=merc +lon_0=150 +k=1 +x_0=0 +y_0=0 +datum=WGS84 +units=m +no_defs",
        "+proj=merc +lon_0=150 +k=1 +x_0=0 +y_0=0 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y,
        src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_29)
{
    double src_x[3] = {9762741.000000, 9762740.000000, 9762742.000000};
    double src_y[3] = {6000256.000000, 6000255.000000, 6000257.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        -13614352.0665874499827623, -13614353.0665874499827623, -13614351.0665874499827623
    };
    double dst_y[3] = {
        6031797.9655712451785803, 6031796.9625006830319762, 6031798.9686418110504746
    };
    run_single_case(
        "+proj=merc +lon_0=150 +k=1 +x_0=0 +y_0=0 +datum=WGS84 +units=m +no_defs",
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_33)
{
    double src_x[3] = {-13614351.981900, -13614352.981900, -13614350.981900};
    double src_y[3] = {6031798.776040, 6031797.776040, 6031799.776040};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        -13614351.9818999972194433, -13614352.9818999972194433, -13614350.9818999972194433
    };
    double dst_y[3] = {
        6000256.8079850031062961, 6000255.8110461672767997, 6000257.8049238389357924
    };
    run_single_case(
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        "+proj=merc +lon_0=0 +k=1 +x_0=0 +y_0=0 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y,
        src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_34)
{
    double src_x[3] = {-13614351.981900, -13614352.981900, -13614350.981900};
    double src_y[3] = {6031798.776040, 6031797.776040, 6031799.776040};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        9762741.0846874546259642, 9762740.0846874546259642, 9762742.0846874546259642
    };
    double dst_y[3] = {
        6000256.8079850031062961, 6000255.8110461672767997, 6000257.8049238389357924
    };
    run_single_case(
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        "+proj=merc +lon_0=150 +k=1 +x_0=0 +y_0=0 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y,
        src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_35)
{
    double src_x[3] = {-13614351.981900, -13614352.981900, -13614350.981900};
    double src_y[3] = {6031798.776040, 6031797.776040, 6031799.776040};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        -13614351.9818999972194433, -13614352.9818999972194433, -13614350.9818999972194433
    };
    double dst_y[3] = {
        6031798.7760399989783764, 6031797.7760399961844087, 6031799.7760400008410215
    };
    run_single_case(
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_39)
{
    double src_x[3] = {119.922611, 118.922611, 120.922611};
    double src_y[3] = {-5.587205, -6.587205, -4.587205};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        13349723.9911198318004608, 13238404.5003265533596277, 13461043.4819130990654230
    };
    double dst_y[3] = {
        -622952.8966558765387163, -734905.0580300039146096, -511191.7312276206794195
    };
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_40)
{
    double src_x[3] = {119.922611, 118.922611, 120.922611};
    double src_y[3] = {-5.587205, -6.587205, -4.587205};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        5000887.9083300884813070, 4889901.7990673081949353, 5111873.9725425075739622
    };
    double dst_y[3] = {282914.8476180526195094, 172037.7573847628664225, 393600.4227953378576785};
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=merc +lon_0=3.192280555555556 +k=0.997 +x_0=3900000 +y_0=900000 +ellps=bessel "
        "+towgs84=-587.8,519.75,145.76,0,0,0,0 +pm=jakarta +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_41)
{
    double src_x[3] = {119.922611, 118.922611, 120.922611};
    double src_y[3] = {-5.587205, -6.587205, -4.587205};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        5000887.9083300866186619, 4889901.7990673072636127, 5111873.9725425057113171
    };
    double dst_y[3] = {282914.8476180526195094, 172037.7573847628664225, 393600.4227953378576785};
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=merc +lon_0=110 +k=0.997 +x_0=3900000 +y_0=900000 +ellps=bessel "
        "+towgs84=-587.8,519.75,145.76,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_45)
{
    double src_x[3] = {13344070.000000, 13344069.999990, 13344070.000010};
    double src_y[3] = {-534447.000000, -534447.000010, -534446.999990};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {119.8718203336078147, 119.8718203335179737, 119.8718203336976558};
    double dst_y[3] = {-4.7954106432458996, -4.7954106433354138, -4.7954106431563863};
    run_single_case(
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(merc, generated_line_49)
{
    double src_x[3] = {4983058.000000, 4983057.999990, 4983058.000010};
    double src_y[3] = {346949.000000, 346948.999990, 346949.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {119.7619583202869080, 119.7619583201967828, 119.7619583203769764};
    double dst_y[3] = {-5.0088753138538298, -5.0088753139441886, -5.0088753137634736};
    run_single_case(
        "+proj=merc +lon_0=3.192280555555556 +k=0.997 +x_0=3900000 +y_0=900000 +ellps=bessel "
        "+towgs84=-587.8,519.75,145.76,0,0,0,0 +pm=jakarta +units=m +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(merc, generated_line_53)
{
    double src_x[3] = {5000738.000000, 5000737.999990, 5000738.000010};
    double src_y[3] = {343316.000000, 343315.999990, 343316.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {119.9212583179526348, 119.9212583178625522, 119.9212583180427316};
    double dst_y[3] = {-5.0417009754529047, -5.0417009755432485, -5.0417009753625575};
    run_single_case(
        "+proj=merc +lon_0=110 +k=0.997 +x_0=3900000 +y_0=900000 +ellps=bessel "
        "+towgs84=-587.8,519.75,145.76,0,0,0,0 +units=m +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(merc, generated_line_57)
{
    double src_x[3] = {13344070.000000, 13344069.000000, 13344071.000000};
    double src_y[3] = {-534447.000000, -534448.000000, -534446.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        4995251.1860215859487653, 4995250.1890173517167568, 4995252.1830258229747415
    };
    double dst_y[3] = {370569.4372965750517324, 370568.4469268776010722, 370570.4276662722695619};
    run_single_case(
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        "+proj=merc +lon_0=3.192280555555556 +k=0.997 +x_0=3900000 +y_0=900000 +ellps=bessel "
        "+towgs84=-587.8,519.75,145.76,0,0,0,0 +pm=jakarta +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_58)
{
    double src_x[3] = {13344070.000000, 13344069.000000, 13344071.000000};
    double src_y[3] = {-534447.000000, -534448.000000, -534446.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        4995251.1860215850174427, 4995250.1890173507854342, 4995252.1830258220434189
    };
    double dst_y[3] = {370569.4372965750517324, 370568.4469268776010722, 370570.4276662722695619};
    run_single_case(
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        "+proj=merc +lon_0=110 +k=0.997 +x_0=3900000 +y_0=900000 +ellps=bessel "
        "+towgs84=-587.8,519.75,145.76,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_62)
{
    double src_x[3] = {5000738.000000, 5000737.000000, 5000739.000000};
    double src_y[3] = {343316.000000, 343315.000000, 343317.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        5000738.0000000018626451, 5000737.0000000018626451, 5000739.0000000009313226
    };
    double dst_y[3] = {343316.0000000465661287, 343315.0000000459840521, 343317.0000000478466973};
    run_single_case(
        "+proj=merc +lon_0=110 +k=0.997 +x_0=3900000 +y_0=900000 +ellps=bessel "
        "+towgs84=-587.8,519.75,145.76,0,0,0,0 +units=m +no_defs",
        "+proj=merc +lon_0=3.192280555555556 +k=0.997 +x_0=3900000 +y_0=900000 +ellps=bessel "
        "+towgs84=-587.8,519.75,145.76,0,0,0,0 +pm=jakarta +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_63)
{
    double src_x[3] = {5000738.000000, 5000737.000000, 5000739.000000};
    double src_y[3] = {343316.000000, 343315.000000, 343317.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        13349573.4112431090325117, 13349572.4082386102527380, 13349574.4142476096749306
    };
    double dst_y[3] = {
        -561965.2695935309166089, -561966.2793125743046403, -561964.2598744863644242
    };
    run_single_case(
        "+proj=merc +lon_0=110 +k=0.997 +x_0=3900000 +y_0=900000 +ellps=bessel "
        "+towgs84=-587.8,519.75,145.76,0,0,0,0 +units=m +no_defs",
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_67)
{
    double src_x[3] = {4983058.000000, 4983057.000000, 4983059.000000};
    double src_y[3] = {346949.000000, 346948.000000, 346950.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        4983058.0000000000000000, 4983056.9999999972060323, 4983059.0000000000000000
    };
    double dst_y[3] = {346949.0000000471482053, 346948.0000000467989594, 346950.0000000467989594};
    run_single_case(
        "+proj=merc +lon_0=3.192280555555556 +k=0.997 +x_0=3900000 +y_0=900000 +ellps=bessel "
        "+towgs84=-587.8,519.75,145.76,0,0,0,0 +pm=jakarta +units=m +no_defs",
        "+proj=merc +lon_0=110 +k=0.997 +x_0=3900000 +y_0=900000 +ellps=bessel "
        "+towgs84=-587.8,519.75,145.76,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(merc, generated_line_68)
{
    double src_x[3] = {4983058.000000, 4983057.000000, 4983059.000000};
    double src_y[3] = {346949.000000, 346948.000000, 346950.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        13331840.2166195884346962, 13331839.2136149760335684, 13331841.2196242008358240
    };
    double dst_y[3] = {
        -558297.0333998125279322, -558298.0431196549907327, -558296.0236799707636237
    };
    run_single_case(
        "+proj=merc +lon_0=3.192280555555556 +k=0.997 +x_0=3900000 +y_0=900000 +ellps=bessel "
        "+towgs84=-587.8,519.75,145.76,0,0,0,0 +pm=jakarta +units=m +no_defs",
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_3)
{
    double src_x[3] = {13.500000, 12.500000, 14.500000};
    double src_y[3] = {47.200000, 46.200000, 48.200000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {12983.5638199103541410, -63965.5544491515174741, 87080.9184574791433988};
    double dst_y[3] = {229022.7886499296873808, 118180.7622209973633289, 340853.2357694236561656};
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=tmerc +lat_0=0 +lon_0=31 +k=1 +x_0=0 +y_0=-5000000 +ellps=bessel "
        "+towgs84=682,-203,480,0,0,0,0 +pm=ferro +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_4)
{
    double src_x[3] = {13.500000, 12.500000, 14.500000};
    double src_y[3] = {47.200000, 46.200000, 48.200000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {462983.5638199103414081, 386034.4455508484970778, 537080.9184574790997431};
    double dst_y[3] = {
        5229022.7886499296873808, 5118180.7622209973633289, 5340853.2357694236561656
    };
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=tmerc +lat_0=0 +lon_0=31 +k=1 +x_0=450000 +y_0=0 +ellps=bessel "
        "+towgs84=682,-203,480,0,0,0,0 +pm=ferro +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_8)
{
    double src_x[3] = {39259.000000, 39258.999990, 39259.000010};
    double src_y[3] = {246227.000000, 246226.999990, 246227.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {13.8482569812119376, 13.8482569810787055, 13.8482569813451644};
    double dst_y[3] = {47.3537101699916292, 47.3537101699022855, 47.3537101700809728};
    run_single_case(
        "+proj=tmerc +lat_0=0 +lon_0=31 +k=1 +x_0=0 +y_0=-5000000 +ellps=bessel "
        "+towgs84=682,-203,480,0,0,0,0 +pm=ferro +units=m +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(tmerc, generated_line_12)
{
    double src_x[3] = {447744.000000, 447743.999990, 447744.000010};
    double src_y[3] = {5247497.000000, 5247496.999990, 5247497.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {13.2987693239003306, 13.2987693237680045, 13.2987693240326585};
    double dst_y[3] = {47.3662845826307262, 47.3662845825407359, 47.3662845827207022};
    run_single_case(
        "+proj=tmerc +lat_0=0 +lon_0=31 +k=1 +x_0=450000 +y_0=0 +ellps=bessel "
        "+towgs84=682,-203,480,0,0,0,0 +pm=ferro +units=m +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(tmerc, generated_line_16)
{
    double src_x[3] = {39259.000000, 39258.000000, 39260.000000};
    double src_y[3] = {246227.000000, 246226.000000, 246228.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {39259.0000000004656613, 39258.0000000003565219, 39260.0000000005020411};
    double dst_y[3] = {246227.0000000009313226, 246226.0000000000000000, 246228.0000000000000000};
    run_single_case(
        "+proj=tmerc +lat_0=0 +lon_0=31 +k=1 +x_0=0 +y_0=-5000000 +ellps=bessel "
        "+towgs84=682,-203,480,0,0,0,0 +pm=ferro +units=m +no_defs",
        "+proj=tmerc +lat_0=0 +lon_0=31 +k=1 +x_0=0 +y_0=-5000000 +ellps=bessel "
        "+towgs84=682,-203,480,0,0,0,0 +pm=ferro +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_17)
{
    double src_x[3] = {39259.000000, 39258.000000, 39260.000000};
    double src_y[3] = {246227.000000, 246226.000000, 246228.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {489259.0000000004656613, 489258.0000000003492460, 489260.0000000005238689};
    double dst_y[3] = {
        5246227.0000000009313226, 5246226.0000000000000000, 5246228.0000000000000000
    };
    run_single_case(
        "+proj=tmerc +lat_0=0 +lon_0=31 +k=1 +x_0=0 +y_0=-5000000 +ellps=bessel "
        "+towgs84=682,-203,480,0,0,0,0 +pm=ferro +units=m +no_defs",
        "+proj=tmerc +lat_0=0 +lon_0=31 +k=1 +x_0=450000 +y_0=0 +ellps=bessel "
        "+towgs84=682,-203,480,0,0,0,0 +pm=ferro +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_18)
{
    double src_x[3] = {39259.000000, 39258.000000, 39260.000000};
    double src_y[3] = {246227.000000, 246226.000000, 246228.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1541580.9155229085590690, 1541579.4324548328295350, 1541582.3985914855729789
    };
    double dst_y[3] = {
        6000000.8984093694016337, 5999999.4302934026345611, 6000002.3665253268554807
    };
    run_single_case(
        "+proj=tmerc +lat_0=0 +lon_0=31 +k=1 +x_0=0 +y_0=-5000000 +ellps=bessel "
        "+towgs84=682,-203,480,0,0,0,0 +pm=ferro +units=m +no_defs",
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_22)
{
    double src_x[3] = {447744.000000, 447743.000000, 447745.000000};
    double src_y[3] = {5247497.000000, 5247496.000000, 5247498.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {-2255.9999999999604370, -2257.0000000000618456, -2255.0000000002355591};
    double dst_y[3] = {247497.0000000000000000, 247496.0000000000000000, 247498.0000000009313226};
    run_single_case(
        "+proj=tmerc +lat_0=0 +lon_0=31 +k=1 +x_0=450000 +y_0=0 +ellps=bessel "
        "+towgs84=682,-203,480,0,0,0,0 +pm=ferro +units=m +no_defs",
        "+proj=tmerc +lat_0=0 +lon_0=31 +k=1 +x_0=0 +y_0=-5000000 +ellps=bessel "
        "+towgs84=682,-203,480,0,0,0,0 +pm=ferro +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_23)
{
    double src_x[3] = {447744.000000, 447743.000000, 447745.000000};
    double src_y[3] = {5247497.000000, 5247496.000000, 5247498.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {447744.0000000000582077, 447742.9999999999417923, 447744.9999999997671694};
    double dst_y[3] = {
        5247497.0000000000000000, 5247496.0000000000000000, 5247498.0000000009313226
    };
    run_single_case(
        "+proj=tmerc +lat_0=0 +lon_0=31 +k=1 +x_0=450000 +y_0=0 +ellps=bessel "
        "+towgs84=682,-203,480,0,0,0,0 +pm=ferro +units=m +no_defs",
        "+proj=tmerc +lat_0=0 +lon_0=31 +k=1 +x_0=450000 +y_0=0 +ellps=bessel "
        "+towgs84=682,-203,480,0,0,0,0 +pm=ferro +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_27)
{
    double src_x[3] = {837002.000000, 837001.999990, 837002.000010};
    double src_y[3] = {818535.000000, 818534.999990, 818535.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {114.1839978456598175, 114.1839978455627715, 114.1839978457568776};
    double dst_y[3] = {22.3057727605488054, 22.3057727604584990, 22.3057727606391083};
    run_single_case(
        "+proj=tmerc +lat_0=22.31213333333334 +lon_0=114.1785555555556 +k=1 +x_0=836694.05 "
        "+y_0=819069.8 +ellps=intl "
        "+towgs84=-162.619,-276.959,-161.764,0.067753,-2.24365,-1.15883,-1.09425 +units=m +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(tmerc, generated_line_31)
{
    double src_x[3] = {834737.000000, 834736.999990, 834737.000010};
    double src_y[3] = {818236.000000, 818235.999990, 818236.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {114.1620162445432243, 114.1620162444461783, 114.1620162446402560};
    double dst_y[3] = {22.3030720900053439, 22.3030720899150303, 22.3030720900956574};
    run_single_case(
        "+proj=tmerc +lat_0=22.31213333333334 +lon_0=114.1785555555556 +k=1 +x_0=836694.05 "
        "+y_0=819069.8 +ellps=intl "
        "+towgs84=-162.619,-276.959,-161.764,0.067753,-2.24365,-1.15883,-1.09425 +units=m +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(tmerc, generated_line_35)
{
    double src_x[3] = {837108.000000, 837107.999990, 837108.000010};
    double src_y[3] = {816301.000000, 816300.999990, 816301.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {114.1850254016639212, 114.1850254015668753, 114.1850254017609529};
    double dst_y[3] = {22.2855983051800237, 22.2855983050897244, 22.2855983052703195};
    run_single_case(
        "+proj=tmerc +lat_0=22.31213333333334 +lon_0=114.1785555555556 +k=1 +x_0=836694.05 "
        "+y_0=819069.8 +ellps=intl "
        "+towgs84=-162.619,-276.959,-161.764,0.067753,-2.24365,-1.15883,-1.09425 +units=m +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(tmerc, generated_line_39)
{
    double src_x[3] = {114.179054, 113.179054, 115.179054};
    double src_y[3] = {22.305479, 21.305479, 23.305479};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {836492.5900815150234848, 732732.8175380927277729, 938784.9506509613711387};
    double dst_y[3] = {818502.4545603966107592, 708102.2113526188768446, 929598.5687918438343331};
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=tmerc +lat_0=22.31213333333334 +lon_0=114.1785555555556 +k=1 +x_0=836694.05 "
        "+y_0=819069.8 +ellps=intl "
        "+towgs84=-162.619,-276.959,-161.764,0.067753,-2.24365,-1.15883,-1.09425 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_43)
{
    double src_x[3] = {114.153390, 113.153390, 115.153390};
    double src_y[3] = {22.283513, 21.283513, 23.283513};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {833847.8042394301155582, 730053.8968003750778735, 936175.8999463864602149};
    double dst_y[3] = {816070.2496374010806903, 705686.8942353943130001, 927147.6925904530799016};
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=tmerc +lat_0=22.31213333333334 +lon_0=114.1785555555556 +k=1 +x_0=836694.05 "
        "+y_0=819069.8 +ellps=intl "
        "+towgs84=-162.619,-276.959,-161.764,0.067753,-2.24365,-1.15883,-1.09425 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_47)
{
    double src_x[3] = {198186.000000, 198185.999990, 198186.000010};
    double src_y[3] = {608042.000000, 608041.999990, 608042.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {34.9804660145809336, 34.9804660144758088, 34.9804660146860442};
    double dst_y[3] = {31.5644921315208649, 31.5644921314304838, 31.5644921316112352};
    run_single_case(
        "+proj=tmerc +lat_0=31.73439361111111 +lon_0=35.20451694444445 +k=1.0000067 "
        "+x_0=219529.584 +y_0=626907.39 +ellps=GRS80 +towgs84=-48,55,52,0,0,0,0 +units=m +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(tmerc, generated_line_51)
{
    double src_x[3] = {34.796069, 33.796069, 35.796069};
    double src_y[3] = {31.949590, 30.949590, 32.949590};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {180839.8082812822831329, 84865.7178328410082031, 274771.7936483275261708};
    double dst_y[3] = {650794.1362553422804922, 540693.8139700194587931, 761772.4490261463215575};
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=tmerc +lat_0=31.73439361111111 +lon_0=35.20451694444445 +k=1.0000067 "
        "+x_0=219529.584 +y_0=626907.39 +ellps=GRS80 +towgs84=-48,55,52,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_55)
{
    double src_x[3] = {494902.000000, 494901.999990, 494902.000010};
    double src_y[3] = {564687.000000, 564686.999990, 564687.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {80.7275303215808151, 80.7275303214902067, 80.7275303216714377};
    double dst_y[3] = {7.5857686606368020, 7.5857686605463615, 7.5857686607272399};
    run_single_case(
        "+proj=tmerc +lat_0=7.000471527777778 +lon_0=80.77171308333334 +k=0.9999238418 +x_0=500000 "
        "+y_0=500000 +a=6377276.345 +b=6356075.41314024 "
        "+towgs84=-0.293,766.95,87.713,0.195704,1.69507,3.47302,-0.039338 +units=m +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(tmerc, generated_line_59)
{
    double src_x[3] = {494902.000000, 494901.000000, 494903.000000};
    double src_y[3] = {564687.000000, 564686.000000, 564688.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {494901.9999999993597157, 494900.9999999998835847, 494903.0000000005820766};
    double dst_y[3] = {564687.0000000002328306, 564686.0000000000000000, 564688.0000000001164153};
    run_single_case(
        "+proj=tmerc +lat_0=7.000471527777778 +lon_0=80.77171308333334 +k=0.9999238418 +x_0=500000 "
        "+y_0=500000 +a=6377276.345 +b=6356075.41314024 "
        "+towgs84=-0.293,766.95,87.713,0.195704,1.69507,3.47302,-0.039338 +units=m +no_defs",
        "+proj=tmerc +lat_0=7.000471527777778 +lon_0=80.77171308333334 +k=0.9999238418 +x_0=500000 "
        "+y_0=500000 +a=6377276.345 +b=6356075.41314024 "
        "+towgs84=-0.293,766.95,87.713,0.195704,1.69507,3.47302,-0.039338 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_60)
{
    double src_x[3] = {494902.000000, 494901.000000, 494903.000000};
    double src_y[3] = {564687.000000, 564686.000000, 564688.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        8986547.5683969277888536, 8986546.5596484616398811, 8986548.5771454349160194
    };
    double dst_y[3] = {846921.7955581987043843, 846920.7799017278011888, 846922.8112146715866402};
    run_single_case(
        "+proj=tmerc +lat_0=7.000471527777778 +lon_0=80.77171308333334 +k=0.9999238418 +x_0=500000 "
        "+y_0=500000 +a=6377276.345 +b=6356075.41314024 "
        "+towgs84=-0.293,766.95,87.713,0.195704,1.69507,3.47302,-0.039338 +units=m +no_defs",
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_64)
{
    double src_x[3] = {80.720451, 79.720451, 81.720451};
    double src_y[3] = {7.744560, 6.744560, 8.744560};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {494123.1362898329971358, 383558.9845701489830390, 604167.0679104099981487};
    double dst_y[3] = {582246.8732244260609150, 471791.0068504788214341, 692963.8075205404311419};
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=tmerc +lat_0=7.000471527777778 +lon_0=80.77171308333334 +k=0.9999238418 +x_0=500000 "
        "+y_0=500000 +a=6377276.345 +b=6356075.41314024 "
        "+towgs84=-0.293,766.95,87.713,0.195704,1.69507,3.47302,-0.039338 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_68)
{
    double src_x[3] = {8965146.000000, 8965145.000000, 8965147.000000};
    double src_y[3] = {848451.000000, 848450.000000, 848452.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {473689.1926617902354337, 473688.2009212644770741, 473690.1844022765289992};
    double dst_y[3] = {566199.8672047908185050, 566198.8830833449028432, 566200.8513262369669974};
    run_single_case(
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        "+proj=tmerc +lat_0=7.000471527777778 +lon_0=80.77171308333334 +k=0.9999238418 +x_0=500000 "
        "+y_0=500000 +a=6377276.345 +b=6356075.41314024 "
        "+towgs84=-0.293,766.95,87.713,0.195704,1.69507,3.47302,-0.039338 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_69)
{
    double src_x[3] = {8965146.000000, 8965145.000000, 8965147.000000};
    double src_y[3] = {848451.000000, 848450.000000, 848452.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        8965146.0000000000000000, 8965145.0000000000000000, 8965147.0000000000000000
    };
    double dst_y[3] = {848450.9999999987194315, 848449.9999999990686774, 848451.9999999990686774};
    run_single_case(
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_73)
{
    double src_x[3] = {200795.000000, 200794.999990, 200795.000010};
    double src_y[3] = {609075.000000, 609074.999990, 609075.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {35.0079273842627288, 35.0079273841575827, 35.0079273843678891};
    double dst_y[3] = {31.5738536304339839, 31.5738536303436312, 31.5738536305243329};
    run_single_case(
        "+proj=tmerc +lat_0=31.73439361111111 +lon_0=35.20451694444445 +k=1.0000067 "
        "+x_0=219529.584 +y_0=626907.39 +ellps=GRS80 +towgs84=-48,55,52,0,0,0,0 +units=m +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(tmerc, generated_line_77)
{
    double src_x[3] = {200795.000000, 200794.000000, 200796.000000};
    double src_y[3] = {609075.000000, 609074.000000, 609076.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {200794.9999999997671694, 200793.9999999997671694, 200795.9999999998835847};
    double dst_y[3] = {609075.0000000005820766, 609074.0000000002328306, 609076.0000000004656613};
    run_single_case(
        "+proj=tmerc +lat_0=31.73439361111111 +lon_0=35.20451694444445 +k=1.0000067 "
        "+x_0=219529.584 +y_0=626907.39 +ellps=GRS80 +towgs84=-48,55,52,0,0,0,0 +units=m +no_defs",
        "+proj=tmerc +lat_0=31.73439361111111 +lon_0=35.20451694444445 +k=1.0000067 "
        "+x_0=219529.584 +y_0=626907.39 +ellps=GRS80 +towgs84=-48,55,52,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_78)
{
    double src_x[3] = {200795.000000, 200794.000000, 200796.000000};
    double src_y[3] = {609075.000000, 609074.000000, 609076.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        3897064.6501440256834030, 3897063.4795912234112620, 3897065.8206970519386232
    };
    double dst_y[3] = {
        3707501.3684456241317093, 3707500.1879325537011027, 3707502.5489586917683482
    };
    run_single_case(
        "+proj=tmerc +lat_0=31.73439361111111 +lon_0=35.20451694444445 +k=1.0000067 "
        "+x_0=219529.584 +y_0=626907.39 +ellps=GRS80 +towgs84=-48,55,52,0,0,0,0 +units=m +no_defs",
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_82)
{
    double src_x[3] = {34.906311, 33.906311, 35.906311};
    double src_y[3] = {31.800558, 30.800558, 32.800558};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {191217.3902328387193847, 95209.1606238318345277, 285190.3908527892199345};
    double dst_y[3] = {634234.5003556563751772, 524040.8465939486632124, 745307.0705658645601943};
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=tmerc +lat_0=31.73439361111111 +lon_0=35.20451694444445 +k=1.0000067 "
        "+x_0=219529.584 +y_0=626907.39 +ellps=GRS80 +towgs84=-48,55,52,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_86)
{
    double src_x[3] = {3877497.000000, 3877496.000000, 3877498.000000};
    double src_y[3] = {3714228.000000, 3714227.000000, 3714229.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {184127.8796251079766080, 184127.0244379584619310, 184128.7348121174145490};
    double dst_y[3] = {614825.1418186343507841, 614824.2965708873234689, 614825.9870663806796074};
    run_single_case(
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        "+proj=tmerc +lat_0=31.73439361111111 +lon_0=35.20451694444445 +k=1.0000067 "
        "+x_0=219529.584 +y_0=626907.39 +ellps=GRS80 +towgs84=-48,55,52,0,0,0,0 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_90)
{
    double src_x[3] = {-3275304.000000, -3275304.000010, -3275303.999990};
    double src_y[3] = {234443.000000, 234442.999990, 234443.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {-94.2407510957638692, -94.2407510957919214, -94.2407510957358028};
    double dst_y[3] = {39.5718162423273370, 39.5718162422961868, 39.5718162423584943};
    run_single_case(
        "+proj=tmerc +lat_0=40 +lon_0=-78.58333333333333 +k=0.9999375 +x_0=350000.0001016001 "
        "+y_0=0 +datum=NAD83 +units=us-ft +no_defs",
        "+proj=longlat +datum=WGS84 +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(tmerc, generated_line_94)
{
    double src_x[3] = {-3275304.000000, -3275305.000000, -3275303.000000};
    double src_y[3] = {234443.000000, 234442.000000, 234444.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        -10490832.4239560719579458, -10490832.7362697627395391, -10490832.1116423532366753
    };
    double dst_y[3] = {
        4803913.4877582993358374, 4803913.0378426760435104, 4803913.9376739505678415
    };
    run_single_case(
        "+proj=tmerc +lat_0=40 +lon_0=-78.58333333333333 +k=0.9999375 +x_0=350000.0001016001 "
        "+y_0=0 +datum=NAD83 +units=us-ft +no_defs",
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_103)
{
    double src_x[3] = {-98.876953, -99.876953, -97.876953};
    double src_y[3] = {43.452919, 42.452919, 44.452919};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        -4244486.4184743184596300, -4607481.6587160164490342, -3889918.2510914728045464
    };
    double dst_y[3] = {
        1929493.6494069867767394, 1633543.3300515364389867, 2228357.2785847508348525
    };
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=tmerc +lat_0=40 +lon_0=-78.58333333333333 +k=0.9999375 +x_0=350000.0001016001 "
        "+y_0=0 +datum=NAD83 +units=us-ft +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(tmerc, generated_line_107)
{
    double src_x[3] = {-11814107.000000, -11814108.000000, -11814106.000000};
    double src_y[3] = {5454546.000000, 5454545.000000, 5454547.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        -6108833.0029409360140562, -6108836.2147867372259498, -6108829.7910957764834166
    };
    double dst_y[3] = {
        2691844.9324471973814070, 2691843.4348370027728379, 2691846.4300574227236211
    };
    run_single_case(
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        "+proj=tmerc +lat_0=40 +lon_0=-78.58333333333333 +k=0.9999375 +x_0=350000.0001016001 "
        "+y_0=0 +datum=NAD83 +units=us-ft +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(utm, generated_line_3)
{
    double src_x[3] = {-370566.000000, -370567.000000, -370565.000000};
    double src_y[3] = {6066042.000000, 6066041.000000, 6066043.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {475352.8572459114948288, 475352.1811138438642956, 475353.5333778232452460};
    double dst_y[3] = {
        5289021.8056848552078009, 5289021.1373102599754930, 5289022.4740594504401088
    };
    run_single_case(
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        "+proj=utm +zone=30 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        0.01);
}

TEST(utm, generated_line_7)
{
    double src_x[3] = {597884.000000, 597883.999990, 597884.000010};
    double src_y[3] = {5329179.000000, 5329178.999990, 5329179.000010};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {-1.6850348605755454, -1.6850348607121424, -1.6850348604389487};
    double dst_y[3] = {48.1083480920254516, 48.1083480919370388, 48.1083480921138644};
    run_single_case(
        "+proj=utm +zone=30 +datum=WGS84 +units=m +no_defs", "+proj=longlat +datum=WGS84 +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 1e-7);
}

TEST(utm, generated_line_11)
{
    double src_x[3] = {597884.000000, 597883.000000, 597885.000000};
    double src_y[3] = {5329179.000000, 5329178.000000, 5329180.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        -187577.2226481831457932, -187578.7432425739825703, -187575.7020532739697956
    };
    double dst_y[3] = {
        6124899.0624633384868503, 6124897.5885464577004313, 6124900.5363801931962371
    };
    run_single_case(
        "+proj=utm +zone=30 +datum=WGS84 +units=m +no_defs",
        "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m "
        "+nadgrids=@null +wktext  +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(utm, generated_line_12)
{
    double src_x[3] = {597884.000000, 597883.000000, 597885.000000};
    double src_y[3] = {5329179.000000, 5329178.000000, 5329180.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {597884.0000000000000000, 597883.0000000000000000, 597885.0000000000000000};
    double dst_y[3] = {
        5329178.9999999990686774, 5329178.0000000000000000, 5329180.0000000000000000
    };
    run_single_case(
        "+proj=utm +zone=30 +datum=WGS84 +units=m +no_defs",
        "+proj=utm +zone=30 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        0.01);
}

TEST(utm, generated_line_13)
{
    double src_x[3] = {597884.000000, 597883.000000, 597885.000000};
    double src_y[3] = {5329179.000000, 5329178.000000, 5329180.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1044357.9172473498620093, 1044356.9951681023230776, 1044358.8393265934428200
    };
    double dst_y[3] = {
        5354277.3143616104498506, 5354276.2355197696015239, 5354278.3932034727185965
    };
    run_single_case(
        "+proj=utm +zone=30 +datum=WGS84 +units=m +no_defs",
        "+proj=utm +zone=29 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        0.01);
}

TEST(utm, generated_line_14)
{
    double src_x[3] = {597884.000000, 597883.000000, 597885.000000};
    double src_y[3] = {5329179.000000, 5329178.000000, 5329180.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {151291.8497080677188933, 151290.7733320197439753, 151292.9260841124923900};
    double dst_y[3] = {
        5338966.9260110128670931, 5338966.0057248091325164, 5338967.8462971942499280
    };
    run_single_case(
        "+proj=utm +zone=30 +datum=WGS84 +units=m +no_defs",
        "+proj=utm +zone=31 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        0.01);
}

TEST(utm, generated_line_18)
{
    double src_x[3] = {-2.760315, -3.760315, -1.760315};
    double src_y[3] = {48.502048, 47.502048, 49.502048};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {517705.2549470044323243, 442740.4406894120038487, 589757.7581133764469996};
    double dst_y[3] = {
        5372130.9605640033259988, 5261237.4612881038337946, 5484006.9630877999588847
    };
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs", "+proj=utm +zone=30 +datum=WGS84 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(utm, generated_line_19)
{
    double src_x[3] = {-2.760315, -3.760315, -1.760315};
    double src_y[3] = {48.502048, 47.502048, 49.502048};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {960806.8687065978301689, 894555.3696186072193086, 1023966.0028126374818385};
    double dst_y[3] = {
        5390931.4555807383731008, 5274276.8389245467260480, 5508503.3290991308167577
    };
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs", "+proj=utm +zone=29 +datum=WGS84 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(utm, generated_line_20)
{
    double src_x[3] = {-2.760315, -3.760315, -1.760315};
    double src_y[3] = {48.502048, 47.502048, 49.502048};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {74579.6483769265469164, -9018.7182413057307713, 155393.5114936066675000};
    double dst_y[3] = {
        5388145.7426114697009325, 5283147.6229929393157363, 5494166.0452412851154804
    };
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs", "+proj=utm +zone=31 +datum=WGS84 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(utm, generated_line_24)
{
    double src_x[3] = {-43.181763, -44.181763, -42.181763};
    double src_y[3] = {-22.919188, -23.919188, -21.919188};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {686468.3994749059202150, 583277.5643760160310194, 791150.5957964598201215};
    double dst_y[3] = {
        7464273.5722857061773539, 7354479.3206881536170840, 7573443.2683527832850814
    };
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=utm +zone=23 +south +ellps=WGS84 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y,
        src_z, dst_x, dst_y, 0.01);
}

TEST(utm, generated_line_25)
{
    double src_x[3] = {-43.181763, -44.181763, -42.181763};
    double src_y[3] = {-22.919188, -23.919188, -21.919188};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {686445.1262171857524663, 583254.5147799084661528, 791127.1004468065220863};
    double dst_y[3] = {
        7464272.9108172385022044, 7354478.5536052361130714, 7573442.7020791200920939
    };
    run_single_case(
        "+proj=longlat +datum=WGS84 +no_defs",
        "+proj=utm +zone=23 +south +ellps=WGS72 +towgs84=0,0,1.9,0,0,0.814,-0.38 +units=m +no_defs",
        3, src_x, src_y, src_z, dst_x, dst_y, 0.01);
}

TEST(utm, generated_line_29)
{
    double src_x[3] = {272128.615811, 272127.615811, 272129.615811};
    double src_y[3] = {2097390.374711, 2097389.374711, 2097391.374711};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {904099.1932029086165130, 904098.2265007854439318, 904100.1599050266668200};
    double dst_y[3] = {
        2100393.5743799875490367, 2100392.5394889572635293, 2100394.6092710532248020
    };
    run_single_case(
        "+proj=utm +zone=43 +ellps=WGS72 +towgs84=0,0,1.9,0,0,0.814,-0.38 +units=m +no_defs",
        "+proj=utm +zone=42 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        0.01);
}

TEST(utm, generated_line_30)
{
    double src_x[3] = {272128.615811, 272127.615811, 272129.615811};
    double src_y[3] = {2097390.374711, 2097389.374711, 2097391.374711};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {272152.3851410577772185, 272151.3851422496372834, 272153.3851398629485629};
    double dst_y[3] = {
        2097392.5441070231609046, 2097391.5441056704148650, 2097393.5441083805635571
    };
    run_single_case(
        "+proj=utm +zone=43 +ellps=WGS72 +towgs84=0,0,1.9,0,0,0.814,-0.38 +units=m +no_defs",
        "+proj=utm +zone=43 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        0.01);
}

TEST(utm, generated_line_31)
{
    double src_x[3] = {272128.615811, 272127.615811, 272129.615811};
    double src_y[3] = {2097390.374711, 2097389.374711, 2097391.374711};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1539581.2790024478454143, 1539580.3380022158380598, 1539582.2200026689097285
    };
    double dst_y[3] = {
        2125184.9732601637952030, 2125183.8935285289771855, 2125186.0529918647371233
    };
    run_single_case(
        "+proj=utm +zone=43 +ellps=WGS72 +towgs84=0,0,1.9,0,0,0.814,-0.38 +units=m +no_defs",
        "+proj=utm +zone=41 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        0.01);
}

TEST(utm, generated_line_35)
{
    double src_x[3] = {272128.000000, 272078.000000, 272178.000000};
    double src_y[3] = {2097390.000000, 2097340.000000, 2097440.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {904098.5896769356913865, 904050.2545688929967582, 904146.9247806033818051};
    double dst_y[3] = {
        2100393.1783747589215636, 2100341.4338621902279556, 2100444.9229654408991337
    };
    run_single_case(
        "+proj=utm +zone=43 +ellps=WGS72 +towgs84=0,0,1.9,0,0,0.814,-0.38 +units=m +no_defs",
        "+proj=utm +zone=42 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        0.5);
}

TEST(utm, generated_line_36)
{
    double src_x[3] = {272128.000000, 272078.000000, 272178.000000};
    double src_y[3] = {2097390.000000, 2097340.000000, 2097440.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {272151.7693304858403280, 272101.7693901824532077, 272201.7692707890528254};
    double dst_y[3] = {
        2097392.1693952102214098, 2097342.1693274518474936, 2097442.1694629704579711
    };
    run_single_case(
        "+proj=utm +zone=43 +ellps=WGS72 +towgs84=0,0,1.9,0,0,0.814,-0.38 +units=m +no_defs",
        "+proj=utm +zone=43 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        0.5);
}

TEST(utm, generated_line_37)
{
    double src_x[3] = {272128.000000, 272078.000000, 272178.000000};
    double src_y[3] = {2097390.000000, 2097340.000000, 2097440.000000};
    double src_z[3] = {0, 0, 0};
    double dst_x[3] = {
        1539580.6828000852838159, 1539533.6327790254727006, 1539627.7328012445941567
    };
    double dst_y[3] = {
        2125184.5519487652927637, 2125130.5654459414072335, 2125238.5386095088906586
    };
    run_single_case(
        "+proj=utm +zone=43 +ellps=WGS72 +towgs84=0,0,1.9,0,0,0.814,-0.38 +units=m +no_defs",
        "+proj=utm +zone=41 +datum=WGS84 +units=m +no_defs", 3, src_x, src_y, src_z, dst_x, dst_y,
        0.5);
}
