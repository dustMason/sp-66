/* Voice models from calibrate.py, with levels fitted to two TR-66 recordings. */
    /* A fast edge and two opposing decays shape the attack. Model the
     * measured second harmonic with half the fundamental's decay constant.
     * The last mode is excited by a coincident snare trigger. */
    [TR66_KICK] = { .tone = {{65.14967f, 42.0f, -1.50000f, -0.09146f},
                             {0.0f, 0.25519f, 0.74485f, 0.0f},
                             {0.0f, 5.8f, 2.29042f, 0.0f},
                             {0.0f, 10.87739f, -1.49877f, 0.0f},
                             {130.29934f, 21.0f, -0.03650f, 0.00850f},
                             {234.0f, 6.5f, 0.0f, 0.0f}} },
    /* The short counter-pulse softens the initial edge. Figure 10 shows two
     * capacitor paths into the snare gate, modeled as held and free decays. */
    [TR66_SNARE] = { .tone = {{292.5f, 12.25f, -0.93459f, 0.09599f}, {0.0f, 1.93792f, 1.13087f, 0.0f},
                              {0.0f, 7.72467f, -0.69879f, 0.0f}, {1100.0f, 0.33f, 0.24771f, 0.39665f},
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
    /* The short mode changes the phase of the first cycles as the claves
     * settle into the main ring. The pulse shapes the initial edge. */
    [TR66_CLAVES] = { .tone = {{2350.0f, 4.75f, 0.175657f, 0.622764f},
                              {0.0f, 0.38f, -0.260405f, 0.0f},
                              {2350.0f, 1.50f, 0.440003f, -0.286140f}} },
    /* The short ring shapes the first cycles of the measured rim attack. */
    [TR66_RIM] = { .tone = {{1525.0f, 2.10f, -1.163728f, -1.118408f},
                           {0.0f, 0.171f, 0.783758f, 0.0f},
                           {1525.0f, 0.60f, -0.202367f, 0.503723f}} },
    /* The pulse offsets the initial voltage. The short mode shapes the
     * first cycles as the two phases settle into the main ring. */
    [TR66_COWBELL] = { .tone = {{895.0f, 13.4f, 0.36217f, 0.44472f},
                                {0.0f, 0.174f, -0.50558f, 0.0f},
                                {895.0f, 2.32f, 0.14341f, -0.12216f}} },
    /* The short fundamental reinforces the attack. The second harmonic
     * decays at twice the rate of the main ring. The pulse offsets its
     * initial amplitude. */
    [TR66_CONGA] = { .tone = {{200.0f, 58.0f, -0.48076f, -0.65717f},
                              {0.0f, 2.08f, 0.38688360f, 0.0f},
                              {200.0f, 11.93f, -0.04524f, -0.06183f},
                              {400.0f, 29.0f, -0.00419360f, -0.00249562f}} },
    /* The main modes use Roland's 600 Hz nominal tuning. The longest
     * mode carries the body. The last mode is driven only in Samba. */
    [TR66_HIGH_BONGO] = { .tone = {{600.0f, 32.0f, -0.035f, -0.23375f},
                                   {600.0f, 5.3f, -0.188f, -0.38325f},
                                   {0.0f, 2.4f, 0.089f, 0.0f}, {600.0f, 7.73f, 0.0f, 0.0f}} },
    /* The first mode is the free ring also excited by the snare trigger.
     * On direct hits the short ring is weaker and lags the free ring
     * by about 11 degrees. */
    [TR66_LOW_BONGO] = { .tone = {{400.0f, 36.0f, -0.09525114f, -0.27536924f},
                                  {0.0f, 2.16f, 0.180090f, 0.0f},
                                  {400.0f, 7.5f, -0.02315386f, -0.17389799f}} },
