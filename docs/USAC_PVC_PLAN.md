# USAC eSBR: kodowanie wektorowe z predykcją obwiedni (PVC) — plan i stan

Status: **zrobione (2026-11-22)** dla trybów 1 i 2, mono (SCE), eSBR 2:1 i 4:1. Szczegóły i wyniki: `docs/AAC_FAMILY_ASM.md` SBR-114. Wzorzec: dekoder libxaac (Apache 2.0, poza repozytorium) — jedyny z dostępnych, który zna PVC (FDK 2.0.3 i FFmpeg nie).

| Etap | Zakres | Stan |
|---|---|---|
| P0 | czy libxaac w ogóle koduje ramki PVC (wzorzec) | tak, tylko mono + rdzeń czasowy: `-usac:2 -ccfl_idx:3 -esbr:1 -pvc_enc:1` |
| P1 | tablice (`tools/gen_usac_pvc_tables.py`), predyktor `sauti_usac_pvc_predict`, test jednostkowy z drugą implementacją | gotowe, 22 / 22 mutantów |
| P2 | składnia: `bs_pvc_mode`, grid PVC, `pvc_id` (div 0 – 7), szum bez obwiedni, sinus + `bs_sine_position`, stan między ramkami | gotowe (`sauti_usac_sbr.c`) |
| P3 | dopasowanie HF: szczelina = obwiednia, nośnik przez całą siatkę, przeniesienie nośnika i szumu, sinus przeniesiony | gotowe (`sauti_aac_sbr_channel.c`) |
| P4 | fixtury i weryfikacja: `usac_p1..p3` (libxaac), `usac_v1..v3` (generator) | gotowe, zob. SBR-114 |

## Co nie ma wzorca (zachowanie = libxaac, bez drugiego źródła)

- Ramka klasyczna po ramce PVC z `bs_var_len_hf > 0`: nikt nie dopasowuje szczelin między nimi (ramka PVC kończy dopasowanie na 16, klasyczna zaczyna od `tb0`); libxaac przepuszcza tam nośnik bez korekcji, my też. Koder tego nie sygnalizuje. (Odwrotny przypadek, PVC po klasycznej z `var_len > 0`, jest określony: klasyczna ramka dopasowuje siatkę do końca, PVC zaczyna od `tb0`; zgodny z libxaac na jednym strumieniu generatora, 4 % w jednej szczelinie.)
- Tryb 1 (koder libxaac używa wyłącznie trybu 2): tablice sprawdzone liczba po liczbie, predyktor i dopasowanie zgodne z libxaac na strumieniach generatora.

## Jak powtórzyć porównanie wnętrza z libxaac

1. Kopia libxaac poza repozytorium (np. `/tmp/claude-1000/xaacdbg/src`) z dopisanym zrzutem w `ixheaacd_sbr_dec.c` (po `ixheaacd_pvc_process`: nagłówek, `pvc_id`, `pvc_qmf_enrg_arr`, `pvc_dec_out_buf`) i w `ixheaacd_esbr_envcal.c` (przed `harm_flag_varlen`: `nrg_gain_pvc`, `noise_level_pvc`, `nrg_tone_pvc` szczelin 0 – 15).
2. Ten sam zrzut po naszej stronie dopisuje się tymczasowo do `sauti_aac_sbr_channel.c` (po `sauti_usac_pvc_predict` i po pętli `adjust_pvc_slot`); w repozytorium go nie ma.
3. Strumienie: `build/sauti_usac_gen ... --pvc 1|2|3|4` (1 – losowe przełączanie klasyczne / tryb 1 / tryb 2, 2 – tylko tryb 1, 3 – tylko tryb 2, 4 – tryby 1 / 2 bez ramek klasycznych); porównanie pomija ramki, w których energie niskiego pasma różnią się już na wejściu predyktora (rdzeń libxaac na poziomie ~1 LSB).

## Odrzucane (zwracają −1 lub 0)

`bs_pvc_mode` 3, PVC w CPE, PVC z rdzeniem 8:3 (`rate` ≠ 2, 4), `bs_var_len_hf` > 3, `pvc_id` poza zakresem (parser czyta 7 bitów, więc nie wystąpi), `bs_noise_position` ≤ `tb0` (pusta obwiednia).
