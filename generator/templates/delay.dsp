declare name "CloudesDelay";
declare description "Delay — echo with feedback (time, feedback, mix)";
import("stdfaust.lib");

dtime = hslider("Time[unit:ms]", 300, 1, 2000, 1) : *(ma.SR / 1000.0) : max(1.0);
fb    = hslider("Feedback", 0.4, 0, 0.95, 0.01) : si.smoo;
mix   = hslider("Mix", 0.3, 0, 1, 0.01) : si.smoo;

maxd = 192000;   // 4 s at 48 kHz
echo = + ~ (de.fdelay(maxd, dtime) : *(fb));

process = par(i, 2, _ <: *(1 - mix), (echo : *(mix)) :> _);
