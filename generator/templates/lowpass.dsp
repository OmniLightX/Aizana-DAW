declare name "CloudesLowpass";
declare description "Template: 2nd-order stereo low-pass (generator fills cutoff)";
import("stdfaust.lib");
cutoff = hslider("Cutoff[unit:Hz][scale:log]", 1000, 20, 20000, 1) : si.smoo;
process = par(i, 2, fi.lowpass(2, cutoff));
