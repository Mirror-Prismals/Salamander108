// Main ChucK bus source intentionally disabled.
// The CHUCK fader currently controls transient ChucK/gameplay output on normal dac channels.
//
// Previous drifting test tone, left here commented for reference:
//
// TriOsc tone => LPF lp => Gain master => dac;
// 0.08 => master.gain;
// 0.35 => tone.gain;
// 1200 => lp.freq;
// 2.0 => lp.Q;
//
// SinOsc slow => blackhole;
// 0.07 => slow.freq;
//
// while (true) {
//     (260.0 + (slow.last() * 90.0)) => tone.freq;
//     (1200.0 + (slow.last() * 450.0)) => lp.freq;
//     10::ms => now;
// }
