/* Voice models from calibrate.py, with levels fitted to two TR-66 recordings. */
    /* A fast edge and two opposing decays shape the attack of the bass drum. */
    [TR66_KICK] = { .tone = {{65.14967f, 40.15f, -1.50000f, -0.09146f}, {0.0f, 0.25519f, 0.70835f, 0.0f},
                             {0.0f, 5.8f, 2.29042f, 0.0f}, {0.0f, 10.87739f, -1.49877f, 0.0f}} },
    /* A short pulse and an opposite-polarity recovery sit under the held noise burst. */
    [TR66_SNARE] = { .tone = {{292.5f, 12.25f, -0.93459f, 0.09599f}, {0.0f, 1.3f, 1.12938f, 0.0f},
                              {401.0f, 36.0f, -0.02659f, -0.13559f}, {0.0f, 7.0f, -0.66442f, 0.0f}},
        .band = {{5625.6526f, 2.0f, 0.16959526f}, {9784.1089f, 0.7326f, 0.45332608f}},
        .noise_attack_ms = 1.7f,
        .noise_fast_ms = 110.0f,
        .noise_fast_mix = 1.0f,
        .noise_fast_hold_ms = 29.0f,
        .noise_threshold = 0.53f },
    /* Two decays form the small pulse under each recorded hi-hat hit. */
    [TR66_HAT] = { .tone = {{0.0f, 0.46f, 0.014f, 0.0f}, {0.0f, 3.5f, -0.014f, 0.0f}},
        .band = {{8061.0f, 11.06f, 6.38068f}, {8330.25390625f, 0.45656088f, 0.16982f}},
        .noise_attack_ms = 3.50f,
        .noise_decay_ms = 62.0f,
        .noise_highpass_hz = 9191.72949219f,
        .noise_threshold = 0.451f },
    /* The corresponding cymbal pulse has the opposite polarity. */
    [TR66_CYMBAL] = { .tone = {{0.0f, 1.4f, -0.080f, 0.0f}, {0.0f, 3.0f, 0.080f, 0.0f}},
        .band = {{8172.25f, 11.18972f, 2.6670002f}, {13097.84f, 0.68986f, 0.0577989f}},
        .noise_attack_ms = 0.85f,
        .noise_decay_ms = 312.0f,
        .noise_highpass_hz = 10100.0f,
        .noise_fast_ms = 7.5f,
        .noise_fast_mix = 0.335f,
        .noise_threshold = 0.155f,
        .noise_sweep_hz = 333.804f,
        .noise_fast_hold_ms = 22.0f },
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
    /* Both recordings have a faster initial decay than the free ring. */
    [TR66_LOW_BONGO] = { .tone = {{400.0f, 32.0f, 0.101331f, 0.292946f},
                                  {0.0f, 2.16f, -0.180090f, 0.0f},
                                  {400.0f, 7.5f, 0.067554f, 0.195298f}} },
