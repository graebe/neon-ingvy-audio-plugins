# References: annotated

The bibliography of [Subtractive Synthesis in Practice](paper.md), with the
numbers the paper cites them by.

Every entry was checked on 2026-10-09 against one of these:
- a DOI registration record (Crossref);
- the publisher's page;
- the DAFx paper archive, the SMC Zenodo community, or the author's or
  university's own page.

The note under each entry says how much of the work was read:

- **full text**: the paper itself was read, and the equations and numbers quoted
  in the survey come from it.
- **abstract**: only the metadata and abstract were read, so the survey cites
  the entry only for what its abstract states.

Entries that could not be verified were left out of the paper.

---

## Peer-reviewed literature, books and technical notes

**[1]** D. Ambrits, B. Bank, "Improved Polynomial Transition Regions Algorithm for Alias-Suppressed Signal Synthesis," *Proc. 10th Sound and Music Computing Conf. (SMC 2013)*, Stockholm, pp. 561–568, 2013. doi:[10.5281/zenodo.850287](https://doi.org/10.5281/zenodo.850287).
- Full text.
- Shows that the output of DPW and PTR is the trivial waveform delayed by half a sample, and removes that delay (EPTR).
- About 30 % fewer operations than PTR.

**[2]** R. Bencina, "Real-time audio programming 101: time waits for nothing," blog post, 5 July 2011. <http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing>
- Grey literature.
- The standard practitioner's account of what an audio callback must not do: locks, allocation, system calls.

**[3]** S. Bilbao, F. Esqueda, J. D. Parker, V. Välimäki, "Antiderivative Antialiasing for Memoryless Nonlinearities," *IEEE Signal Processing Letters*, vol. 24, no. 7, pp. 1049–1053, 2017. doi:[10.1109/LSP.2017.2675541](https://doi.org/10.1109/LSP.2017.2675541).
- Full text.
- Extends ADAA to higher orders.
- At 2× oversampling, second- and third-order ADAA on a hard clipper improve the SNR by about 15 and 30 dB over 6× oversampling.

**[4]** E. Brandt, "Hard Sync Without Aliasing," *Proc. Int. Computer Music Conf. (ICMC 2001)*, Havana, pp. 365–368, 2001. <https://www.cs.cmu.edu/~eli/papers/icmc01-hardsync.pdf>
- Full text.
- Introduces BLEP and minBLEP: a band-limited, minimum-phase step mixed in at each discontinuity, needing no look-ahead.

**[5]** S. D'Angelo, V. Välimäki, "An Improved Virtual Analog Model of the Moog Ladder Filter," *Proc. IEEE ICASSP 2013*, pp. 729–733, 2013. doi:[10.1109/ICASSP.2013.6637744](https://doi.org/10.1109/ICASSP.2013.6637744).
- Abstract.
- A circuit-derived nonlinear ladder that self-oscillates realistically, at 12 more operations per sample than [17].

**[6]** S. D'Angelo, V. Välimäki, "Generalized Moog Ladder Filter: Part I – Linear Analysis and Parameterization," *IEEE/ACM Trans. Audio, Speech, Lang. Process.*, vol. 22, no. 12, pp. 1825–1832, 2014. doi:[10.1109/TASLP.2014.2352495](https://doi.org/10.1109/TASLP.2014.2352495).
- Full text.
- The DC gain is −1/(1+k) for any number of stages.
- Explains why four stages put the resonance exactly at the cutoff.

**[7]** S. D'Angelo, V. Välimäki, "Generalized Moog Ladder Filter: Part II – Explicit Nonlinear Model through a Novel Delay-Free Loop Implementation Method," *IEEE/ACM Trans. Audio, Speech, Lang. Process.*, vol. 22, no. 12, pp. 1873–1883, 2014. doi:[10.1109/TASLP.2014.2352556](https://doi.org/10.1109/TASLP.2014.2352556).
- Abstract.
- A non-iterative delay-free-loop method that keeps the linear response exact around an operating point.

**[8]** L. de Soras, "Denormal numbers in floating point signal processing applications," technical note, 2002. <http://ldesoras.free.fr/doc/articles/denormal-en.pdf>
- Grey literature.
- Explains the performance cost of subnormal floats in recursive DSP and how to avoid it.

**[9]** F. Esqueda, S. Bilbao, V. Välimäki, "Aliasing Reduction in Clipped Signals," *IEEE Trans. Signal Processing*, vol. 64, no. 20, pp. 5255–5267, 2016. doi:[10.1109/TSP.2016.2585091](https://doi.org/10.1109/TSP.2016.2585091).
- Full text.
- A polyBLAMP correction at each clipping corner.
- Improves the SNR by 12 dB (2-point) or 20 dB (4-point) on average.

**[10]** F. Esqueda, H. Pöntynen, J. D. Parker, S. Bilbao, "Virtual Analog Models of the Lockhart and Serge Wavefolders," *Applied Sciences*, vol. 7, no. 12, art. 1328, 2017. doi:[10.3390/app7121328](https://doi.org/10.3390/app7121328).
- Abstract.
- Wavefolders in closed form (Lambert-W), made real-time by first-order ADAA.

**[11]** F. Esqueda, V. Välimäki, S. Bilbao, "Rounding Corners with BLAMP," *Proc. 19th Int. Conf. Digital Audio Effects (DAFx-16)*, Brno, pp. 121–128, 2016. <https://dafx.de/paper-archive/2016/dafxpapers/18-DAFx-16_paper_33-PN.pdf>
- Full text.
- polyBLAMP for discontinuities in the first derivative: triangle waves and corners.
- Reduces aliased components by up to 50 dB.

**[12]** F. Fontana, M. Civolani, "Modeling of the EMS VCS3 Voltage-Controlled Filter as a Nonlinear Filter Network," *IEEE Trans. Audio, Speech, Lang. Process.*, vol. 18, no. 4, pp. 760–772, 2010. doi:[10.1109/TASL.2010.2046287](https://doi.org/10.1109/TASL.2010.2046287).
- Abstract.
- Delay-free loops in a nonlinear diode-ladder network, solved by fixed-point iteration at 176.4 kHz.

**[13]** A. Franck, V. Välimäki, "Higher-Order Integrated Wavetable Synthesis," *Proc. 15th Int. Conf. Digital Audio Effects (DAFx-12)*, York, 2012. <https://dafx.de/paper-archive/2012/papers/dafx12_submission_69.pdf>
- Full text.
- The table is integrated K times offline and differentiated K times after playback.
- The cost is independent of pitch; the limit is quantisation noise as K rises.

**[14]** G. Geiger, "Table Lookup Oscillators Using Generic Integrated Wavetables," *Proc. 9th Int. Conf. Digital Audio Effects (DAFx-06)*, Montréal, pp. 169–172, 2006. <https://dafx.de/paper-archive/2006/papers/p_169.pdf>
- Full text.
- DPW generalised to arbitrary wavetables.

**[15]** M. Herlihy, "Wait-free synchronization," *ACM Trans. Programming Languages and Systems*, vol. 13, no. 1, pp. 124–149, 1991. doi:[10.1145/114005.102808](https://doi.org/10.1145/114005.102808).
- Metadata.
- The theory behind the lock-free queues that carry parameters to an audio thread.

**[16]** M. Holters, "Antiderivative Antialiasing for Stateful Systems," *Applied Sciences*, vol. 10, no. 1, art. 20, 2020 (extended version of the DAFx-19 paper). doi:[10.3390/app10010020](https://doi.org/10.3390/app10010020).
- Full text.
- ADAA inside feedback adds half a sample to the loop; designing the coefficients for (2/3)·fs compensates for it.

**[17]** A. Huovilainen, "Non-Linear Digital Implementation of the Moog Ladder Filter," *Proc. 7th Int. Conf. Digital Audio Effects (DAFx-04)*, Naples, pp. 61–64, 2004. <https://dafx.de/paper-archive/2004/P_061.PDF>
- Full text.
- Per-stage tanh nonlinearities, 5 tanh per sample.
- A unit delay in the feedback, plus a half-sample average to correct the tuning; 2× oversampling.

**[18]** J. F. Kaiser, R. W. Schafer, "On the Use of the I0-sinh Window for Spectrum Analysis," *IEEE Trans. Acoustics, Speech, Signal Process.*, vol. 28, no. 1, pp. 105–107, 1980. doi:[10.1109/TASSP.1980.1163349](https://doi.org/10.1109/TASSP.1980.1163349).
- Metadata.
- The Kaiser window used for the half-band filter in the survey.

**[19]** J. Kleimola, V. Välimäki, "Reducing Aliasing from Synthetic Audio Signals Using Polynomial Transition Regions," *IEEE Signal Processing Letters*, vol. 19, no. 2, pp. 67–70, 2012. doi:[10.1109/LSP.2011.2177819](https://doi.org/10.1109/LSP.2011.2177819).
- Abstract.
- DPW output is computed only in the transition samples.
- At least 40 % fewer operations than DPW.

**[20]** P. P. La Pastina, S. D'Angelo, "A General Antialiasing Method for Sine Hard Sync," *Proc. 25th Int. Conf. Digital Audio Effects (DAFx20in22)*, Vienna, pp. 109–114, 2022. <https://dafx.de/paper-archive/2022/papers/DAFx20in22_paper_3.pdf>
- Full text.
- A finite-support residual for the infinite-order discontinuity of a reset sine.

**[21]** J. Laroche, "On the Stability of Time-Varying Recursive Filters," *J. Audio Eng. Soc.*, vol. 55, no. 6, pp. 460–471, 2007.
- Metadata.
- Stability criteria for filters whose coefficients change while they run.

**[22]** H.-M. Lehtonen, J. Pekonen, V. Välimäki, "Audibility of aliasing distortion in sawtooth signals and its implications for oscillator algorithm design," *J. Acoust. Soc. Am.*, vol. 132, no. 4, pp. 2721–2733, 2012. doi:[10.1121/1.4748964](https://doi.org/10.1121/1.4748964).
- Metadata.
- Listening tests on the audibility of aliasing in sawtooth signals.

**[23]** J. Nam, V. Välimäki, J. S. Abel, J. O. Smith, "Efficient Antialiasing Oscillator Algorithms Using Low-Order Fractional Delay Filters," *IEEE Trans. Audio, Speech, Lang. Process.*, vol. 18, no. 4, pp. 773–785, 2010. doi:[10.1109/TASL.2009.2035039](https://doi.org/10.1109/TASL.2009.2035039).
- Full text.
- Builds each BLIT pulse from a low-order fractional-delay filter.
- Perceptually alias-free above C8.
- Components above about 15 kHz are inaudible for these waveforms.

**[24]** K. Nielsen, "Practical Linear and Exponential Frequency Modulation for Digital Music Synthesis," *Proc. 23rd Int. Conf. Digital Audio Effects (DAFx2020)*, Vienna, pp. 132–139, 2020. <https://www.dafx.de/paper-archive/2020/proceedings/papers/DAFx2020_paper_61.pdf>
- Full text.
- Limits the FM index by Carson's rule to keep the sidebands below Nyquist.

**[25]** J. D. Parker, V. Zavalishin, E. Le Bivic, "Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution," *Proc. 19th Int. Conf. Digital Audio Effects (DAFx-16)*, Brno, pp. 137–144, 2016. <https://dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf>
- Full text.
- Introduces ADAA.
- For a hard clipper at equal aliasing: 12× oversampling without ADAA, 4× with first-order ADAA.

**[26]** P. A. Regalia, S. K. Mitra, P. P. Vaidyanathan, "The Digital All-Pass Filter: A Versatile Signal Processing Building Block," *Proc. IEEE*, vol. 76, no. 1, pp. 19–37, 1988. doi:[10.1109/5.3286](https://doi.org/10.1109/5.3286).
- Metadata.
- All-pass structures, including the polyphase IIR half-band.

**[27]** J. Roth, D. Keller, O. Castañeda, C. Studer, "Alias-Free Oscillator Synchronization via Additive Synthesis," *Proc. 29th Int. Conf. Digital Audio Effects (DAFx26)*, pp. 396–403, 2026. <https://dafx.de/paper-archive/2026/papers/DAFx26_paper_49.pdf>
- Full text.
- Hard and soft sync of arbitrary (wavetable) waveforms, by linear maps on Fourier coefficients.

**[28]** J. Schimmel, "Audible Aliasing Distortion in Digital Audio Synthesis," *Radioengineering*, vol. 21, no. 1, pp. 56–62, 2012. <https://www.radioeng.cz/fulltexts/2012/12_01_0056_0062.pdf>
- Title page and abstract.
- Assesses audible aliasing with a simultaneous-masking model.

**[29]** S. Shan, L. Hantrakul, J. Chen, M. Avent, D. Trevelyan, "Differentiable Wavetable Synthesis," *Proc. IEEE ICASSP 2022*, pp. 4598–4602, 2022. doi:[10.1109/ICASSP43922.2022.9746940](https://doi.org/10.1109/ICASSP43922.2022.9746940).
- Abstract.
- Wavetables learned end to end.

**[30]** A. Simper, "Linear Trapezoidal Integrated SVF," Cytomic technical note, 2013 (rev. 2016). <https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf>
- Full text; not peer-reviewed.
- The trapezoidal SVF derived by nodal analysis.
- Equivalent in form to [42].

**[31]** J. O. Smith III, "Digital Audio Resampling Home Page," CCRMA, Stanford University, 2020. <https://ccrma.stanford.edu/~jos/resample/>
- Full text.
- Band-limited interpolation from a table-stored, Kaiser-windowed sinc.

**[32]** T. Stilson, J. O. Smith, "Alias-Free Digital Synthesis of Classic Analog Waveforms," *Proc. Int. Computer Music Conf. (ICMC 1996)*, Hong Kong, pp. 332–335, 1996. <https://ccrma.stanford.edu/~stilti/papers/blit.pdf>
- Full text.
- Introduces BLIT.
- §3.2: table lookup above unit increment is a decimation, which is the formal case for band-limited table sets.

**[33]** T. Stilson, J. O. Smith, "Analyzing the Moog VCF with Considerations for Digital Implementation," *Proc. Int. Computer Music Conf. (ICMC 1996)*, Hong Kong, pp. 398–401, 1996. <https://ccrma.stanford.edu/~stilti/papers/moogvcf.pdf>
- Full text.
- H(s) = 1/(k + (1 + s/ωc)⁴), self-oscillation at k = 4, passband loss of about 1/(1+k).
- The bilinear transform gives a delay-free loop.

**[34]** A. Szabo, *How to Emulate the Super Saw*, B.Sc. thesis, KTH Royal Institute of Technology, report TRITA-CSC-E 2010:131, 2010. Archived at <https://web.archive.org/web/20170201235019/https://www.nada.kth.se/utbildning/grukth/exjobb/rapportlistor/2010/rapporter10/szabo_adam_10131.pdf>
- Full text.
- Analysis of the JP-8000 seven-oscillator Super Saw, with its non-linear detune curve.

**[35]** R. A. Valenzuela, A. G. Constantinides, "Digital Signal Processing Schemes for Efficient Interpolation and Decimation," *IEE Proc. G*, vol. 130, no. 6, pp. 225–235, 1983. doi:[10.1049/ip-g-1.1983.0044](https://doi.org/10.1049/ip-g-1.1983.0044).
- Metadata.
- The origin of the polyphase all-pass half-band for ×2 rate changes.

**[36]** V. Välimäki, "Discrete-Time Synthesis of the Sawtooth Waveform With Reduced Aliasing," *IEEE Signal Processing Letters*, vol. 12, no. 3, pp. 214–217, 2005. doi:[10.1109/LSP.2004.842271](https://doi.org/10.1109/LSP.2004.842271).
- Abstract.
- DPW, with scaling c = fs/(4f0).
- +10 dB SNR over the trivial saw (+15 dB with 2× oversampling).

**[37]** V. Välimäki, A. Huovilainen, "Oscillator and Filter Algorithms for Virtual Analog Synthesis," *Computer Music Journal*, vol. 30, no. 2, pp. 19–31, 2006. doi:[10.1162/comj.2006.30.2.19](https://doi.org/10.1162/comj.2006.30.2.19).
- Metadata.
- A tutorial overview of VA oscillators and filters.

**[38]** V. Välimäki, A. Huovilainen, "Antialiasing Oscillators in Subtractive Synthesis," *IEEE Signal Processing Magazine*, vol. 24, no. 2, pp. 116–125, 2007. doi:[10.1109/MSP.2007.323276](https://doi.org/10.1109/MSP.2007.323276).
- Abstract.
- The three-way taxonomy: band-limited, quasi-band-limited, alias-suppressing.
- Introduces the polynomial BLEP.

**[39]** V. Välimäki, J. Nam, J. O. Smith, J. S. Abel, "Alias-Suppressed Oscillators Based on Differentiated Polynomial Waveforms," *IEEE Trans. Audio, Speech, Lang. Process.*, vol. 18, no. 4, pp. 786–798, 2010. doi:[10.1109/TASL.2009.2026507](https://doi.org/10.1109/TASL.2009.2026507).
- Abstract.
- DPW of order N.
- Fourth order is perceptually alias-free over the whole piano range.

**[40]** V. Välimäki, J. Pekonen, J. Nam, "Perceptually informed synthesis of bandlimited classical waveforms using integrated polynomial interpolation," *J. Acoust. Soc. Am.*, vol. 131, no. 1, pp. 974–986, 2012. doi:[10.1121/1.3651227](https://doi.org/10.1121/1.3651227).
- Full text.
- The PolyBLEP family.
- The 2-point PolyBLEP is perceptually alias-free up to f0 ≈ 2.1 kHz at 44.1 kHz; the 4-point B-spline version up to 7.8 kHz.

**[41]** A. Wishnick, "Time-Varying Filters for Musical Applications," *Proc. 17th Int. Conf. Digital Audio Effects (DAFx-14)*, Erlangen, 2014. <https://dafx.de/paper-archive/2014/dafx14_aaron_wishnick_time_varying_filters_for_.pdf>
- Full text.
- Proves the trapezoidal SVF stable under arbitrary time variation of its parameters.

**[42]** V. Zavalishin, *The Art of VA Filter Design*, rev. 2.1.2, 2020. <https://archive.org/details/the-art-of-va-filter-design-rev.-2.1.2>
- Full text.
- The topology-preserving transform and zero-delay feedback.
- The TPT SVF and ladder, nonlinear ZDF solutions, and ADAA (§6.13).
- The original Native Instruments URL no longer resolves; the book's licence allows verbatim copies.

---

## Products and their documentation

The survey describes commercial and open-source synthesizers only through
what their own documentation states. Where the survey reads a product's
public source tree, it reports structure and named constants, never code, and
anything it infers from them is worded as an inference.

**[S1]** M. Tytel, *Vital*, source repository, GPL-3.0-or-later. <https://github.com/mtytel/vital>
- Accessed 2026-10-09. Read: README, LICENSE, the directory tree, and the named constants in public headers.

**[S2]** Vital Audio, product site. <https://vital.audio/>
- Accessed 2026-10-09.

**[S3]** Xfer Records, *Serum 2 User Guide*, version 2.0.18, manual version 1.0.3, 27 April 2025. <https://xferrecords.com/manual/serum-2>

**[S4]** Xfer Records, *Serum* product page, archived 1 January 2024. <https://web.archive.org/web/20240101114006/https://xferrecords.com/products/serum/>

**[S5]** Surge Synth Team, *Surge XT User Manual*. <https://surge-synthesizer.github.io/manual-xt/>
- Accessed 2026-10-09. Surge XT is GPL-3.0-or-later.
