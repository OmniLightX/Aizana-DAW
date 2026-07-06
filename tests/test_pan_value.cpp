// test_pan_value.cpp — UX4: Pan-Wert-Format/-Parser (Doppelklick-Eingabe im Mixer).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/common/PanValue.h"

using namespace aizana::panvalue;

TEST_CASE ("UX4: format — S1-Konvention L/C/R", "[panvalue][ux4]")
{
    CHECK (format ( 0.0)   == "C");
    CHECK (format (-0.33)  == "L33");
    CHECK (format ( 0.48)  == "R48");
    CHECK (format (-1.0)   == "L100");
    CHECK (format ( 1.0)   == "R100");
    CHECK (format ( 0.004) == "C");     // rundet auf 0
}

TEST_CASE ("UX4: parse — L/R-Prefix, C, nackte Zahlen, Clamp, Garbage", "[panvalue][ux4]")
{
    double v = 99.0;

    REQUIRE (parse ("L45", v));   CHECK (v == Catch::Approx (-0.45));
    REQUIRE (parse ("r30", v));   CHECK (v == Catch::Approx ( 0.30));
    REQUIRE (parse (" C ", v));   CHECK (v == Catch::Approx ( 0.0));
    REQUIRE (parse ("0", v));     CHECK (v == Catch::Approx ( 0.0));
    REQUIRE (parse ("-20", v));   CHECK (v == Catch::Approx (-0.20));
    REQUIRE (parse ("75", v));    CHECK (v == Catch::Approx ( 0.75));
    REQUIRE (parse ("L150", v));  CHECK (v == Catch::Approx (-1.0));   // clamp 100
    REQUIRE (parse ("999", v));   CHECK (v == Catch::Approx ( 1.0));   // clamp

    CHECK_FALSE (parse ("", v));
    CHECK_FALSE (parse ("Lx", v));
    CHECK_FALSE (parse ("hallo", v));
    CHECK_FALSE (parse ("L", v));
    CHECK_FALSE (parse ("12.5x", v));

    // Roundtrip: format→parse ist verlustfrei (Prozent-Raster).
    for (double p : { -1.0, -0.35, 0.0, 0.5, 1.0 })
    {
        double back = 99.0;
        REQUIRE (parse (format (p), back));
        CHECK (back == Catch::Approx (p));
    }
}
