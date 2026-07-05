declare name "AizanaReverb";
declare description "Reverb — room/hall ambience (size, damping, mix)";
import("stdfaust.lib");

size = hslider("Size",    0.5, 0, 1, 0.01) : si.smoo;
damp = hslider("Damping", 0.5, 0, 1, 0.01) : si.smoo;
mix  = hslider("Mix",     0.3, 0, 1, 0.01) : si.smoo;

wet = re.stereo_freeverb(size, size, damp, 0.5);
process = _, _ <: (*(1 - mix), *(1 - mix)),
                  (wet : *(mix), *(mix)) :> _, _;
