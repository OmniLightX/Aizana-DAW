declare name "AizanaEQ3";
declare description "3-band EQ — low shelf, sweepable mid bell, high shelf";
import("stdfaust.lib");

lg = hslider("Low[unit:dB]",  0, -18, 18, 0.1) : si.smoo;
mg = hslider("Mid[unit:dB]",  0, -18, 18, 0.1) : si.smoo;
mf = hslider("Mid Freq[unit:Hz][scale:log]", 1000, 200, 8000, 1);
hg = hslider("High[unit:dB]", 0, -18, 18, 0.1) : si.smoo;

band = fi.low_shelf(lg, 200) : fi.peak_eq(mg, mf, mf/2.0) : fi.high_shelf(hg, 4000);
process = par(i, 2, band);
