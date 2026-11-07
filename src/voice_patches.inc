/* Voice models from calibrate.py, with levels fitted to two TR-66 recordings. */
    /* A fast edge and two opposing decays shape the attack. Model the
     * measured second harmonic with half the fundamental's decay constant. */
    [TR66_KICK] = { .tone = {{65.14967f, 42.0f, -1.50000f, -0.09146f},
                             {0.0f, 0.25519f, 0.74485f, 0.0f},
                             {0.0f, 5.8f, 2.29042f, 0.0f},
                             {0.0f, 10.87739f, -1.49877f, 0.0f},
                             {130.29934f, 21.0f, -0.03650f, 0.00850f}} },
    /* The short counter-pulse softens the initial edge. Figure 10 shows two
     * capacitor paths into the snare gate, modeled as held and free decays. */
    [TR66_SNARE] = { .tone = {{292.5f, 12.25f, -0.93459f, 0.09599f}, {0.0f, 1.3f, 1.12938f, 0.0f},
                              {0.0f, 7.0f, -0.66442f, 0.0f}, {1100.0f, 0.33f, 0.24771f, 0.39665f},
                              {0.0f, 0.065f, 0.105f, 0.0f}},
        .band = {{5625.6526f, 2.0f, 0.62936240f}, {9784.1089f, 0.7326f, 1.68227809f}},
        .noise_attack_ms = 0.8117643f,
        .noise_decay_ms = 124.660310f,
        .noise_fast_ms = 554.858109f,
        .noise_fast_mix = 0.89912584f,
        .noise_fast_hold_ms = 42.3951202f,
        .noise_threshold = 0.86513498f },
    /* Two decays form the small pulse under each recorded hi-hat hit. */
    [TR66_HAT] = { .tone = {{0.0f, 0.46f, 0.014f, 0.0f}, {0.0f, 3.5f, -0.014f, 0.0f}},
        .band = {{8350.0f, 13.5f, 6.8f}, {8330.25390625f, 0.45656088f, 0.1635f}},
        .noise_attack_ms = 3.35f,
        .noise_decay_ms = 62.0f,
        .noise_highpass_hz = 9050.0f,
        .noise_threshold = 0.451f,
        .noise_gate_curve = 6.4f,
        .noise_sweep_hz = 540.0f,
        .noise_sweep_ms = 33.0f,
        .noise_start_q = 9.4f,
        .noise_damping_ms = 25.5f },
    /* The cymbal's low pulse has a short positive peak and a slower
     * negative undershoot, as measured on isolated hits in both packs. */
    [TR66_CYMBAL] = { .tone = {{0.0f, 0.95f, -0.0532f, 0.0f},
                              {0.0f, 6.6f, 0.1900f, 0.0f},
                              {0.0f, 13.5f, -0.1368f, 0.0f}},
        .band = {{8172.25f, 11.18972f, 2.514f}, {13097.84f, 0.68986f, 0.047f}},
        .noise_attack_ms = 0.85f,
        .noise_decay_ms = 312.0f,
        .noise_highpass_hz = 9700.0f,
        .noise_fast_ms = 7.5f,
        .noise_fast_mix = 0.335f,
        .noise_threshold = 0.155f,
        .noise_sweep_hz = 540.0f,
        .noise_fast_hold_ms = 22.0f,
        .noise_sweep_ms = 19.0f,
        .noise_start_q = 5.56815f,
        .noise_damping_ms = 8.0f },
    /* The claves and rim shot are passive resonators with a short trigger pulse. */
    [TR66_CLAVES] = { .tone = {{2350.0f, 4.75f, 0.63888f, 0.10258f},
                              {0.0f, 0.09f, -0.97832f, 0.0f}} },
    [TR66_RIM] = { .tone = {{1525.0f, 2.10f, -1.00154f, -0.93159f},
                           {0.0f, 0.18f, 0.70f, 0.0f}} },
    [TR66_COWBELL] = { .tone = {{895.0f, 13.4f, -0.53993481f, 0.19342791f}, {0.00000000f, 0.10489128f, 0.56115299f, 0.00000000f}} },
    /* A short decay reinforces the first cycles of the high conga. */
    [TR66_CONGA] = { .tone = {{200.0f, 58.0f, -0.48076f, -0.65717f},
                              {0.0f, 2.08f, 0.38269f, 0.0f},
                              {200.0f, 11.93f, -0.04524f, -0.06183f}} },
    /* Both modes use Roland's 600 Hz nominal tuning. The longer mode
     * carries more of the body of the hit as the fast mode decays. */
    [TR66_HIGH_BONGO] = { .tone = {{600.0f, 32.0f, -0.035f, -0.23375f},
                                   {600.0f, 5.3f, -0.188f, -0.38325f}, {0.0f, 2.4f, 0.089f, 0.0f}} },
    /* The first mode is the free ring also excited by the snare trigger.
     * The direct drive polarity is measured relative to the conga. */
    [TR66_LOW_BONGO] = { .tone = {{400.0f, 36.0f, -0.09525114f, -0.27536924f},
                                  {0.0f, 2.16f, 0.180090f, 0.0f},
                                  {400.0f, 7.5f, -0.067554f, -0.195298f}} },
