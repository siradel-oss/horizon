#include <hrz_common_geo.h>

#include <gtest/gtest.h>
#include <lin_maths.h>
#include <proj_lite.h>

#include <limits>

TEST(CommonGeoMaths, geo_to_web_mercator)
{
    static const float eps = 0.01;

    {
        auto out = hrz::geo_to_web_mercator(hrz::GeoPosition2{0, 0});

        EXPECT_NEAR(out.x, 0, eps);
        EXPECT_NEAR(out.y, 0, eps);
    }
    {
        auto out =
            hrz::geo_to_web_mercator(hrz::GeoPosition2{lm::radians(48.4), lm::radians(157.3)});

        EXPECT_NEAR(out.x, 17510555.90, eps);
        EXPECT_NEAR(out.y, 6173660.45, eps);
    }
    {
        auto out =
            hrz::geo_to_web_mercator(hrz::GeoPosition2{lm::radians(76.4), lm::radians(-68.7)});

        EXPECT_NEAR(out.x, -7647649.02, eps);
        EXPECT_NEAR(out.y, 13563705.90, eps);
    }
    {
        auto out =
            hrz::geo_to_web_mercator(hrz::GeoPosition2{lm::radians(-36.1), lm::radians(24.7)});

        EXPECT_NEAR(out.x, 2749591.42, eps);
        EXPECT_NEAR(out.y, -4314389.96, eps);
    }
    {
        auto out =
            hrz::geo_to_web_mercator(hrz::GeoPosition2{lm::radians(-35.9), lm::radians(-178.4)});

        EXPECT_NEAR(out.x, -19859397.16, eps);
        EXPECT_NEAR(out.y, -4286870.24, eps);
    }
    {
        auto out =
            hrz::geo_to_web_mercator(hrz::GeoPosition2{lm::radians(-86.6), lm::radians(-87.4)});

        EXPECT_NEAR(out.x, -9729323.50, eps);
        EXPECT_NEAR(out.y, -22433854.47, eps);
    }
    {
        auto out = hrz::geo_to_web_mercator(hrz::GeoPosition2{lm::radians(90.0), lm::radians(0.0)});

        EXPECT_NEAR(out.x, 0, eps);
        EXPECT_EQ(out.y, std::numeric_limits<double>::infinity());
    }
    {
        auto out =
            hrz::geo_to_web_mercator(hrz::GeoPosition2{lm::radians(-90.0), lm::radians(0.0)});

        EXPECT_NEAR(out.x, 0, eps);
        EXPECT_EQ(out.y, -std::numeric_limits<double>::infinity());
    }
    {
        auto out = hrz::geo_to_web_mercator(
            hrz::GeoBounds(lm::radians(0.0), lm::radians(0.0), lm::radians(0.0), lm::radians(0.0)));

        EXPECT_NEAR(out.min.x, 0, eps);
        EXPECT_NEAR(out.min.y, 0, eps);
        EXPECT_NEAR(out.max.x, 0, eps);
        EXPECT_NEAR(out.max.y, 0, eps);
    }
    {
        auto out = hrz::geo_to_web_mercator(
            hrz::GeoBounds(lm::radians(0), lm::radians(0), lm::radians(-90.0), lm::radians(90.0)));

        EXPECT_NEAR(out.min.x, 0, eps);
        EXPECT_EQ(out.min.y, -std::numeric_limits<double>::infinity());
        EXPECT_NEAR(out.max.x, 0, eps);
        EXPECT_EQ(out.max.y, std::numeric_limits<double>::infinity());
    }
    {
        auto out = hrz::geo_to_web_mercator(hrz::GeoBounds(
            lm::radians(-178.4), lm::radians(24.7), lm::radians(-86.6), lm::radians(-35.9)));

        EXPECT_NEAR(out.min.x, -19859397.16, eps);
        EXPECT_NEAR(out.min.y, -22433854.47, eps);
        EXPECT_NEAR(out.max.x, 2749591.42, eps);
        EXPECT_NEAR(out.max.y, -4286870.24, eps);
    }
}

TEST(CommonGeoMaths, web_mercator_to_geo2)
{
    static const float eps = 1e-7;

    {
        auto out = hrz::web_mercator_to_geo2({0, 0});

        EXPECT_NEAR(out.lat, 0, eps);
        EXPECT_NEAR(out.lon, 0, eps);
    }
    {
        auto out = hrz::web_mercator_to_geo2({17510555.90, 6173660.45});

        EXPECT_NEAR(out.lat, lm::radians(48.4), eps);
        EXPECT_NEAR(out.lon, lm::radians(157.3), eps);
    }
    {
        auto out = hrz::web_mercator_to_geo2({-7647649.02, 13563705.90});

        EXPECT_NEAR(out.lat, lm::radians(76.4), eps);
        EXPECT_NEAR(out.lon, lm::radians(-68.7), eps);
    }
    {
        auto out = hrz::web_mercator_to_geo2({2749591.42, -4314389.96});

        EXPECT_NEAR(out.lat, lm::radians(-36.1), eps);
        EXPECT_NEAR(out.lon, lm::radians(24.7), eps);
    }
    {
        auto out = hrz::web_mercator_to_geo2({-19859397.16, -4286870.24});

        EXPECT_NEAR(out.lat, lm::radians(-35.9), eps);
        EXPECT_NEAR(out.lon, lm::radians(-178.4), eps);
    }
    {
        auto out = hrz::web_mercator_to_geo2({-9729323.50, -22433854.47});

        EXPECT_NEAR(out.lat, lm::radians(-86.6), eps);
        EXPECT_NEAR(out.lon, lm::radians(-87.4), eps);
    }
}

TEST(CommonGeoMaths, geo_to_ecef)
{
    static const float eps = 0.01;

    {
        auto out = hrz::geo_to_ecef({0, 0, 0});

        EXPECT_NEAR(out.x, 6378137, eps);
        EXPECT_NEAR(out.y, 0, eps);
        EXPECT_NEAR(out.z, 0, eps);
    }
    {
        auto out = hrz::geo_to_ecef({0, 0, 1000});

        EXPECT_NEAR(out.x, 6379137, eps);
        EXPECT_NEAR(out.y, 0, eps);
        EXPECT_NEAR(out.z, 0, eps);
    }
    {
        auto out = hrz::geo_to_ecef({lm::radians(-90.0), lm::radians(180.0), 0});

        EXPECT_NEAR(out.x, 0, eps);
        EXPECT_NEAR(out.y, 0, eps);
        EXPECT_NEAR(out.z, -6356752.31415482, eps);
    }
    {
        auto out = hrz::geo_to_ecef({lm::radians(48.1), lm::radians(-4.0), 153});

        EXPECT_NEAR(out.x, 4257154.83182715, eps);
        EXPECT_NEAR(out.y, -297689.265339584, eps);
        EXPECT_NEAR(out.z, 4724423.14485365, eps);
    }
    {
        auto out = hrz::geo_to_ecef({lm::radians(88.1), lm::radians(-163.8), 2346});

        EXPECT_NEAR(out.x, -203828.73453023, eps);
        EXPECT_NEAR(out.y, -59217.7215547121, eps);
        EXPECT_NEAR(out.z, 6355578.64935798, eps);
    }
    {
        auto out = hrz::geo_to_ecef({lm::radians(-53.7), lm::radians(-111.7), -203});

        EXPECT_NEAR(out.x, -1399142.80629763, eps);
        EXPECT_NEAR(out.y, -3515890.61427775, eps);
        EXPECT_NEAR(out.z, -5116883.05850939, eps);
    }
    {
        auto out = hrz::geo_to_ecef({lm::radians(28.4), lm::radians(93.4), -10000});

        EXPECT_NEAR(out.x, -332470.090149656, eps);
        EXPECT_NEAR(out.y, 5596108.23541082, eps);
        EXPECT_NEAR(out.z, 3010816.32142, eps);
    }
}

TEST(CommonGeoMaths, ecef_to_geo3)
{
    static const float eps = 1e-7;
    static const float eps_m = 0.01;

    {
        auto out = hrz::ecef_to_geo3({6378137, 0, 0});

        EXPECT_NEAR(out.lat, 0, eps);
        EXPECT_NEAR(out.lon, 0, eps);
        EXPECT_NEAR(out.alt, 0, eps_m);
    }
    {
        auto out = hrz::ecef_to_geo3({6379137, 0, 0});

        EXPECT_NEAR(out.lat, 0, eps);
        EXPECT_NEAR(out.lon, 0, eps);
        EXPECT_NEAR(out.alt, 1000, eps_m);
    }
    {
        auto out = hrz::ecef_to_geo3({0, 0, -6356752.31415482});

        EXPECT_NEAR(out.lat, lm::radians(-90.0), eps);
        EXPECT_NEAR(out.lon, lm::radians(0.0), eps);
        EXPECT_NEAR(out.alt, 0, eps_m);
    }
    {
        auto out = hrz::ecef_to_geo3({4257154.83182715, -297689.265339584, 4724423.14485365});

        EXPECT_NEAR(out.lat, lm::radians(48.1), eps);
        EXPECT_NEAR(out.lon, lm::radians(-4.0), eps);
        EXPECT_NEAR(out.alt, 153, eps_m);
    }
    {
        auto out = hrz::ecef_to_geo3({-203828.73453023, -59217.7215547121, 6355578.64935798});

        EXPECT_NEAR(out.lat, lm::radians(88.1), eps);
        EXPECT_NEAR(out.lon, lm::radians(-163.8), eps);
        EXPECT_NEAR(out.alt, 2346, eps_m);
    }
    {
        auto out = hrz::ecef_to_geo3({-1399142.80629763, -3515890.61427775, -5116883.05850939});

        EXPECT_NEAR(out.lat, lm::radians(-53.7), eps);
        EXPECT_NEAR(out.lon, lm::radians(-111.7), eps);
        EXPECT_NEAR(out.alt, -203, eps_m);
    }
    {
        auto out = hrz::ecef_to_geo3({-332470.090149656, 5596108.23541082, 3010816.32142});

        EXPECT_NEAR(out.lat, lm::radians(28.4), eps);
        EXPECT_NEAR(out.lon, lm::radians(93.4), eps);
        EXPECT_NEAR(out.alt, -10000, eps_m);
    }
}

TEST(CommonGeoMaths, ecef_to_geo2)
{
    static const float eps = 1e-7;

    {
        auto out = hrz::ecef_to_geo2({6378137, 0, 0});

        EXPECT_NEAR(out.lat, 0, eps);
        EXPECT_NEAR(out.lon, 0, eps);
    }
    {
        auto out = hrz::ecef_to_geo2({6379137, 0, 0});

        EXPECT_NEAR(out.lat, 0, eps);
        EXPECT_NEAR(out.lon, 0, eps);
    }
    {
        auto out = hrz::ecef_to_geo2({0, 0, -6356752.31415482});

        EXPECT_NEAR(out.lat, lm::radians(-90.0), eps);
        EXPECT_NEAR(out.lon, lm::radians(0.0), eps);
    }
    {
        auto out = hrz::ecef_to_geo2({4257154.83182715, -297689.265339584, 4724423.14485365});

        EXPECT_NEAR(out.lat, lm::radians(48.1), eps);
        EXPECT_NEAR(out.lon, lm::radians(-4.0), eps);
    }
    {
        auto out = hrz::ecef_to_geo2({-203828.73453023, -59217.7215547121, 6355578.64935798});

        EXPECT_NEAR(out.lat, lm::radians(88.1), eps);
        EXPECT_NEAR(out.lon, lm::radians(-163.8), eps);
    }
    {
        auto out = hrz::ecef_to_geo2({-1399142.80629763, -3515890.61427775, -5116883.05850939});

        EXPECT_NEAR(out.lat, lm::radians(-53.7), eps);
        EXPECT_NEAR(out.lon, lm::radians(-111.7), eps);
    }
    {
        auto out = hrz::ecef_to_geo2({-332470.090149656, 5596108.23541082, 3010816.32142});

        EXPECT_NEAR(out.lat, lm::radians(28.4), eps);
        EXPECT_NEAR(out.lon, lm::radians(93.4), eps);
    }
}

TEST(CommonGeoMaths, wgs_84_bounds_intersect)
{
    {
        hrz::GeoBounds a = {
            lm::radians(-90.0), lm::radians(90.0), lm::radians(-45.0), lm::radians(45.0)};
        hrz::GeoBounds b = {
            lm::radians(-20.0), lm::radians(20.0), lm::radians(-10.0), lm::radians(10.0)};

        EXPECT_EQ(hrz::intersect(a, b), true);
        EXPECT_EQ(hrz::intersect(b, a), true);
    }
    {
        hrz::GeoBounds a = {
            lm::radians(-90.0), lm::radians(90.0), lm::radians(-45.0), lm::radians(45.0)};
        hrz::GeoBounds b = {
            lm::radians(-120.0), lm::radians(-100.0), lm::radians(-10.0), lm::radians(10.0)};

        EXPECT_EQ(hrz::intersect(a, b), false);
        EXPECT_EQ(hrz::intersect(b, a), false);
    }
    {
        hrz::GeoBounds a = {
            lm::radians(-90.0), lm::radians(90.0), lm::radians(-45.0), lm::radians(45.0)};
        hrz::GeoBounds b = {
            lm::radians(100.0), lm::radians(120.0), lm::radians(-10.0), lm::radians(10.0)};

        EXPECT_EQ(hrz::intersect(a, b), false);
        EXPECT_EQ(hrz::intersect(b, a), false);
    }
    {
        hrz::GeoBounds a = {
            lm::radians(-90.0), lm::radians(90.0), lm::radians(-45.0), lm::radians(45.0)};
        hrz::GeoBounds b = {
            lm::radians(-20.0), lm::radians(20.0), lm::radians(-60.0), lm::radians(-50.0)};

        EXPECT_EQ(hrz::intersect(a, b), false);
        EXPECT_EQ(hrz::intersect(b, a), false);
    }
    {
        hrz::GeoBounds a = {
            lm::radians(-90.0), lm::radians(90.0), lm::radians(-45.0), lm::radians(45.0)};
        hrz::GeoBounds b = {
            lm::radians(-20.0), lm::radians(20.0), lm::radians(50.0), lm::radians(60.0)};

        EXPECT_EQ(hrz::intersect(a, b), false);
        EXPECT_EQ(hrz::intersect(b, a), false);
    }
    {
        hrz::GeoBounds a = {
            lm::radians(-90.0), lm::radians(10.0), lm::radians(-45.0), lm::radians(45.0)};
        hrz::GeoBounds b = {
            lm::radians(-60.0), lm::radians(20.0), lm::radians(-10.0), lm::radians(10.0)};

        EXPECT_EQ(hrz::intersect(a, b), true);
        EXPECT_EQ(hrz::intersect(b, a), true);
    }
    {
        hrz::GeoBounds a = {
            lm::radians(90.0), lm::radians(-90.0), lm::radians(-45.0), lm::radians(45.0)};
        hrz::GeoBounds b = {
            lm::radians(-20.0), lm::radians(20.0), lm::radians(-10.0), lm::radians(10.0)};

        EXPECT_EQ(hrz::intersect(a, b), false);
        EXPECT_EQ(hrz::intersect(b, a), false);
    }
    {
        hrz::GeoBounds a = {
            lm::radians(90.0), lm::radians(-90.0), lm::radians(-45.0), lm::radians(45.0)};
        hrz::GeoBounds b = {
            lm::radians(20.0), lm::radians(-20.0), lm::radians(-10.0), lm::radians(10.0)};

        EXPECT_EQ(hrz::intersect(a, b), true);
        EXPECT_EQ(hrz::intersect(b, a), true);
    }
    {
        hrz::GeoBounds a = {
            lm::radians(90.0), lm::radians(-90.0), lm::radians(-45.0), lm::radians(45.0)};
        hrz::GeoBounds b = {
            lm::radians(100.0), lm::radians(120.0), lm::radians(-10.0), lm::radians(10.0)};

        EXPECT_EQ(hrz::intersect(a, b), true);
        EXPECT_EQ(hrz::intersect(b, a), true);
    }
    {
        hrz::GeoBounds a = {
            lm::radians(90.0), lm::radians(-90.0), lm::radians(-45.0), lm::radians(45.0)};
        hrz::GeoBounds b = {
            lm::radians(-120.0), lm::radians(-100.0), lm::radians(-10.0), lm::radians(10.0)};

        EXPECT_EQ(hrz::intersect(a, b), true);
        EXPECT_EQ(hrz::intersect(b, a), true);
    }
    {
        hrz::GeoBounds a = {
            lm::radians(90.0), lm::radians(-90.0), lm::radians(-45.0), lm::radians(45.0)};
        hrz::GeoBounds b = {
            lm::radians(-100.0), lm::radians(-120.0), lm::radians(-10.0), lm::radians(10.0)};

        EXPECT_EQ(hrz::intersect(a, b), true);
        EXPECT_EQ(hrz::intersect(b, a), true);
    }
    {
        hrz::GeoBounds a = {
            lm::radians(-90.0), lm::radians(90.0), lm::radians(45.0), lm::radians(-45.0)};
        hrz::GeoBounds b = {
            lm::radians(-20.0), lm::radians(20.0), lm::radians(-10.0), lm::radians(10.0)};

        EXPECT_EQ(hrz::intersect(a, b), false);
        EXPECT_EQ(hrz::intersect(b, a), false);
    }
    {
        hrz::GeoBounds a = {
            lm::radians(-90.0), lm::radians(90.0), lm::radians(-45.0), lm::radians(45.0)};
        hrz::GeoBounds b = {
            lm::radians(-20.0), lm::radians(20.0), lm::radians(10.0), lm::radians(-10.0)};

        EXPECT_EQ(hrz::intersect(a, b), false);
        EXPECT_EQ(hrz::intersect(b, a), false);
    }
    {
        hrz::GeoBounds a = {
            lm::radians(90.0), lm::radians(-90.0), lm::radians(45.0), lm::radians(-45.0)};
        hrz::GeoBounds b = {
            lm::radians(-100.0), lm::radians(-120.0), lm::radians(-10.0), lm::radians(10.0)};

        EXPECT_EQ(hrz::intersect(a, b), false);
        EXPECT_EQ(hrz::intersect(b, a), false);
    }
    {
        hrz::GeoBounds a = {
            lm::radians(90.0), lm::radians(-90.0), lm::radians(-45.0), lm::radians(45.0)};
        hrz::GeoBounds b = {
            lm::radians(-100.0), lm::radians(-120.0), lm::radians(10.0), lm::radians(-10.0)};

        EXPECT_EQ(hrz::intersect(a, b), false);
        EXPECT_EQ(hrz::intersect(b, a), false);
    }
    {
        hrz::GeoBounds a = {
            lm::radians(-10.0), lm::radians(10.0), lm::radians(-10.0), lm::radians(10.0)};
        hrz::GeoBounds b = {lm::radians(0.0), lm::radians(0.0), lm::radians(0.0), lm::radians(0.0)};

        EXPECT_EQ(hrz::intersect(a, b), true);
        EXPECT_EQ(hrz::intersect(b, a), true);
    }
    {
        hrz::GeoBounds a = {
            lm::radians(10.0), lm::radians(-10.0), lm::radians(-10.0), lm::radians(10.0)};
        hrz::GeoBounds b = {
            lm::radians(-120.0), lm::radians(-120.0), lm::radians(0.0), lm::radians(0.0)};

        EXPECT_EQ(hrz::intersect(a, b), true);
        EXPECT_EQ(hrz::intersect(b, a), true);
    }
}
