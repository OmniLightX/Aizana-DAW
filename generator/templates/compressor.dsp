declare name "AizanaCompressor";
declare description "Compressor — tames dynamics (threshold, ratio, attack, release, makeup)";
import("stdfaust.lib");

thresh = hslider("Threshold[unit:dB]", -18, -60, 0, 0.1);
ratio  = hslider("Ratio", 4, 1, 20, 0.1);
att    = hslider("Attack[unit:ms]", 10, 0.1, 100, 0.1) : *(0.001);
rel    = hslider("Release[unit:ms]", 120, 1, 1000, 1) : *(0.001);
makeup = hslider("Makeup[unit:dB]", 0, 0, 24, 0.1) : ba.db2linear : si.smoo;

process = co.compressor_stereo(ratio, thresh, att, rel) : *(makeup), *(makeup);
