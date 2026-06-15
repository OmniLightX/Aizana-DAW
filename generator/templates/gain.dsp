declare name "CloudesGain";
declare description "Template: simple stereo gain (generator fills the range/default)";
import("stdfaust.lib");
gain = hslider("Gain[unit:dB]", 0, -60, 12, 0.1) : ba.db2linear : si.smoo;
process = par(i, 2, *(gain));
