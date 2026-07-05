declare name "AizanaDrive";
declare description "Template: soft-clip saturation (generator fills drive amount)";
import("stdfaust.lib");
drive = hslider("Drive", 0.5, 0, 1, 0.01) : si.smoo;
process = par(i, 2, ef.cubicnl(drive, 0));
