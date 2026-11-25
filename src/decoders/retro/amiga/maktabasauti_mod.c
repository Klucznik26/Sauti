/* MaktabaSauti, modul maktabasauti_mod: odtwarzacz modulow ProTracker / SoundTracker
 * (15 i 31 instrumentow, 1..99 kanalow).
 *
 * Kod jest nasz: uklad bajtow, zegar Pauli i regulamin taktu wynikaja z publicznego opisu formatu, nie z kodu
 * ProTrackera, libxmp, OpenMPTa, MikModa ani FFmpeg.  Modul nie linkuje sie z zadna z tych bibliotek.
 *
 * Kawalek 1:
 *   - magiki M.K. i FLT4 (do 64 patternow) oraz M!K! i 4CHN (do 256);
 *   - odmiany pliku, nie wycinek: 31 instrumentow (M.K., M&K!, FLT4, EXO4, N.T. do 64 patternow;
 *     M!K!, PATT, NSMS, LARD do 256; xCHN 1..9; xxCH i xxCN 10..99; TDZ1..TDZ9;
 *     CD61 6 kanalow; CD81, OCTA, OKTA 8; FA04/FA06/FA08 to 4/6/8),
 *     FLT8, EXO8 i EX08 (8 kanalow, para patternow 4-kanalowych) i 15 instrumentow bez sygnatury;
 *   - nieznana czteroznakowa sygnatura jest odrzucana i nie jest czytana jako 15 instrumentow.
 *     Sam bajt dlugosci utworu nie wystarcza: kazda glosnosc musi byc 0..64, a kazdy order ponizej 64;
 *   - uciety pattern, probka za koncem pliku, petla poza probka i zla dlugosc utworu: blad, nic nie gra;
 *   - komorka niesie okres z tabeli finetune 0.  Niezerowy finetune zamienia go na okres z kolumny
 *     tego strojenia.  Arpeggio i glissando ida po opublikowanej tabeli, nie po wzorze 2^(-ft/96);
 *   - numer instrumentu bez nuty zmienia glosnosc od razu.  Grajaca probka konczy przebieg,
 *     potem wchodzi petla zatrzasku albo kanal milknie.  Petla na juz milczacym kanale startuje od razu.
 *     Pozniejsza nuta gra zatrzasnieta probke od poczatku;
 *   - dzialaja: 0xy arpeggio (000 zostaje brakiem efektu), 1xx/2xx porta, 3xx tone porta,
 *     4xy vibrato, 5xy, 6xy, 7xy tremolo, 8xx panorama 0..255, 9xx offset, Axy,
 *     Cxx, Fxx oraz Exx.  E00 wlacza filtr LED 3275 Hz, kazdy inny E0x go gasi;
 *   - 1xx/2xx i E1x/E2x stoja na okresie 113..856.  Slizg, vibrato, tremolo i arpeggio ida od taktu 1.
 *     E1x, E2x, EAx, EBx i ECx wykonuja sie juz w takcie 0.  Parametr 0 powtarza ostatni niezerowy.
 *     Bxx po wierszu skacze na pozycje order.  Dxx jest dziesietny.
 *     E6x powtarza kawalek patternu ograniczona liczbe razy.  Skok Bxx/Dxx na wiersz, ktory juz gral, konczy utwor.
 *     EFx co takt dodaje przyrost 0,5,6,7,8,10,11,13,16,19,22,26,32,43,64,128.
 *     Licznik od 128 wraca do zera i odwraca jeden bajt petli (XOR 0xFF), potem indeks rosnie.
 *     EF0 gasi efekt i zeruje licznik.  Bez petli nic sie nie zmienia.  Maska jest prywatna, plik zostaje.
 *     Fala 3 jest kwadratem, tym samym co fala 2.  Dokumentacja mowi o losowej, ProTracker gra kwadrat;
 *   - po ostatniej pozycji order bajt restartu wznawia te pozycje, jesli jej wiersz 0 jeszcze nie gral.
 *     0x78 i 0x7F nie sa pozycja.  Wartosc spoza dlugosci utworu tez nie.  Powrot na wiersz, ktory juz gral, konczy utwor;
 *   - wyjscie to splot stereo S16.  Panorama powtarza pary Amigi: 4k i 4k+3 na lewo, 4k+1 i 4k+2 na prawo;
 *   - miks w int32, nasycenie do int16.  Domyslny zegar PAL Pauli 3546895 Hz.
 *     Opcja mod.clock ntsc wybiera NTSC 3579545 Hz.  Plik nie zapisuje zegara.
 *
 * Czas taktu: 2,5 / BPM sekundy (przy 125 BPM i 44100 Hz wychodzi 882 klatki).  Fxx z predkoscia 1..31
 * obowiazuje juz ten wiersz.  Fxx z tempem BPM odmierza biezacy takt starym tempem, a nastepne nowym.
 * W module 15-instrumentowym kazde niezerowe Fxx jest predkoscia.  F00 zatrzymuje sekwencer: petla
 * nie ma konca, jednorazowa probka konczy sie, gdy zamilknie.  Reszta z dzielenia przechodzi dalej.
 *
 * Faza probki: po klatce i (od zera) indeks = floor(i * zegar / (okres * czestotliwosc)).
 * Petla: poczatek probki gra raz, potem odcinek petli.  Petla od bajtu 0 i krotsza niz probka
 * tez gra cala probke raz.  Dlugosc petli 0 albo 1 slowo = brak petli.
 * 9xx dodaje offset do zagrania i drugi raz do poczatku nastepnej nuty.  Nowy instrument zeruje ten poczatek.
 * Nowy okres zeruje faze.  Sam numer instrumentu bez okresu nie zeruje fazy.
 * Przy odczycie, domyslnie, dwa pierwsze bajty kazdej probki sa zerem (ProTracker 1.1).
 * Opcja mod.tracker pt10 zeruje cztery i gra stary Funk Repeat zamiast XOR.
 * Opcja ust remapuje efekty Ultimate Soundtrackera i nie zeruje prefiksu.
 * st2 to SoundTracker II-VI: Dxy slajd, Exy slajd trwaly do parametru 00, F z dolnego nibble.
 * st9 to SoundTracker IX i Master: Bxx, lamanie patternu zawsze na wiersz 0, E0x filtr.
 * tjc to Jungle Command: efekty 3..B moduluja nastepny kanal, predkosc 5, piec przejsc arpeggia.
 * st23 i st24 to DOC 2.3/2.4: komendy Mastera plus Axy, petla w bajtach, bez wzoru tempa.
 * st25 to Soundtracker 2.5: komendy NoiseTrackera, petla w slowach, F modulo 32.
 * Mod's Grave nie wynika z rozmiaru.  Osobny deskryptor gra osiem kanalow, gdy nazwa konczy sie na .wow.
 * Plik nie zapisuje wersji, wiec domyslny dzwiek zostaje przy 1.1.  Plik zostaje.
 * EFx odwraca bajt juz po tym zerze, wiec bajt 0 z zera staje sie 0xFF.
 * Suma glosow jest w NASM.  RC i filtr LED zostaja w C.
 */
#include "sauti_decoder_mod.h"
#include <string.h>

extern void maktabasauti_mod_voice_add(int32_t acc[2], const uint8_t *sample, const uint8_t *flip,
                                       uint32_t idx, uint32_t length, uint32_t clear_n,
                                       int32_t vol, uint32_t pan);

#define TRACK_PT11 0u
#define TRACK_PT10 1u
#define TRACK_UST 2u
#define TRACK_ST2 3u
#define TRACK_ST9 4u
#define TRACK_TJC 5u
#define TRACK_ST23 6u
#define TRACK_ST25 7u

#define MOD_INST 31u
#define MOD_HEADER_31 1084u
#define MOD_HEADER_15 600u
#define MOD_CH_MAX 99u
#define PAULA_PAL 3546895u
#define PAULA_NTSC 3579545u

typedef struct {
    const uint8_t *data;
    uint8_t *flip; /* bit 1: bajt odwrocony przez EFx.  data zostaje zmapowanym plikiem. */
    uint32_t length;
    uint32_t loop_start;
    uint32_t loop_len;
    uint8_t volume;
    int8_t finetune;
    char name[23];
} inst_t;

typedef struct {
    const uint8_t *sample;
    uint32_t length;
    uint32_t loop_start;
    uint32_t loop_len;
    const uint8_t *held; /* zatrzask: nastepny przebieg albo trigger, nie biezacy dzwiek */
    uint32_t held_len;
    uint32_t held_loop_start;
    uint32_t held_loop_len;
    uint32_t idx;
    uint64_t frac;
    uint16_t period;
    uint16_t play_period;
    uint16_t target;
    uint8_t volume;
    uint8_t effect;
    uint8_t param;
    uint8_t inst;
    uint8_t porta_up, porta_down, tone_speed;
    uint8_t fine_up, fine_down;
    uint8_t fine_vol_up, fine_vol_down;
    uint8_t vib_pos, vib_speed, vib_depth, vib_wave;
    uint8_t trem_pos, trem_speed, trem_depth, trem_wave;
    int16_t tremolo;
    uint8_t vol_slide;
    uint8_t auto_slide; /* Exy grup II-VI.  Parametr 00 gasi auto_on. */
    uint8_t auto_on;
    uint8_t mod_vol; /* TJC: slowo DMA ustawia glosnosc nastepnego kanalu */
    uint8_t mod_per; /* TJC: slowo DMA ustawia okres nastepnego kanalu */
    uint8_t mod_silent; /* kanal modulatora nie wydaje dzwieku */
    uint16_t note_period; /* okres z prawdziwego triggera, baza arpeggia st23 */
    uint32_t mod_idx; /* indeks slowa modulatora, nie bajtu probki */
    uint64_t mod_frac;
    uint8_t offset;
    uint32_t start; /* kumulowany poczatek 9xx, w probkach.  Zatrzask instrumentu zeruje. */
    uint8_t loop_once;
    uint8_t retrig;
    uint8_t pan;
    uint8_t gliss;
    uint8_t delay_tick;
    uint8_t delay_sample;
    uint16_t delay_period;
    int8_t tune;
    uint8_t inv_speed;
    uint16_t inv_acc;
    uint32_t inv_pos;
    uint32_t wave_pos; /* kursor Funk Repeat (pt10).  SetDMA go nie cofa. */
    uint32_t real_loop;
} voice_t;

/* 16 strojen, po 60 okresow: oktawa pod C-1, C-1..B-3 i oktawa ponizej B-3.
 * Indeks 0..7 to finetune +0..+7, 8..15 to -8..-1.  Zrodlo: opublikowana mt_PeriodTable. */
#define MOD_NOTES 60
static const uint16_t k_periods[16][MOD_NOTES] = {
    /* +0 */
    {
        1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016,  960,  906,
         856,  808,  762,  720,  678,  640,  604,  570,  538,  508,  480,  453,
         428,  404,  381,  360,  339,  320,  302,  285,  269,  254,  240,  226,
         214,  202,  190,  180,  170,  160,  151,  143,  135,  127,  120,  113,
         107,  101,   95,   90,   85,   80,   75,   71,   67,   63,   60,   56,
    },
    /* +1 */
    {
        1700, 1604, 1514, 1430, 1348, 1274, 1202, 1134, 1070, 1010,  954,  900,
         850,  802,  757,  715,  674,  637,  601,  567,  535,  505,  477,  450,
         425,  401,  379,  357,  337,  318,  300,  284,  268,  253,  239,  225,
         213,  201,  189,  179,  169,  159,  150,  142,  134,  126,  119,  113,
         106,  100,   94,   89,   84,   79,   75,   71,   67,   63,   59,   56,
    },
    /* +2 */
    {
        1688, 1592, 1504, 1418, 1340, 1264, 1194, 1126, 1064, 1004,  948,  894,
         844,  796,  752,  709,  670,  632,  597,  563,  532,  502,  474,  447,
         422,  398,  376,  355,  335,  316,  298,  282,  266,  251,  237,  224,
         211,  199,  188,  177,  167,  158,  149,  141,  133,  125,  118,  112,
         105,   99,   94,   88,   83,   79,   74,   70,   66,   62,   59,   56,
    },
    /* +3 */
    {
        1676, 1582, 1492, 1408, 1330, 1256, 1184, 1118, 1056,  996,  940,  888,
         838,  791,  746,  704,  665,  628,  592,  559,  528,  498,  470,  444,
         419,  395,  373,  352,  332,  314,  296,  280,  264,  249,  235,  222,
         209,  198,  187,  176,  166,  157,  148,  140,  132,  125,  118,  111,
         104,   99,   93,   88,   83,   78,   74,   70,   66,   62,   59,   55,
    },
    /* +4 */
    {
        1664, 1570, 1482, 1398, 1320, 1246, 1176, 1110, 1048,  990,  934,  882,
         832,  785,  741,  699,  660,  623,  588,  555,  524,  495,  467,  441,
         416,  392,  370,  350,  330,  312,  294,  278,  262,  247,  233,  220,
         208,  196,  185,  175,  165,  156,  147,  139,  131,  124,  117,  110,
         104,   98,   92,   87,   82,   78,   73,   69,   65,   62,   58,   55,
    },
    /* +5 */
    {
        1652, 1558, 1472, 1388, 1310, 1238, 1168, 1102, 1040,  982,  926,  874,
         826,  779,  736,  694,  655,  619,  584,  551,  520,  491,  463,  437,
         413,  390,  368,  347,  328,  309,  292,  276,  260,  245,  232,  219,
         206,  195,  184,  174,  164,  155,  146,  138,  130,  123,  116,  109,
         103,   97,   92,   87,   82,   77,   73,   69,   65,   61,   58,   54,
    },
    /* +6 */
    {
        1640, 1548, 1460, 1378, 1302, 1228, 1160, 1094, 1032,  974,  920,  868,
         820,  774,  730,  689,  651,  614,  580,  547,  516,  487,  460,  434,
         410,  387,  365,  345,  325,  307,  290,  274,  258,  244,  230,  217,
         205,  193,  183,  172,  163,  154,  145,  137,  129,  122,  115,  109,
         102,   96,   91,   86,   81,   77,   72,   68,   64,   61,   57,   54,
    },
    /* +7 */
    {
        1628, 1536, 1450, 1368, 1292, 1220, 1150, 1086, 1026,  968,  914,  862,
         814,  768,  725,  684,  646,  610,  575,  543,  513,  484,  457,  431,
         407,  384,  363,  342,  323,  305,  288,  272,  256,  242,  228,  216,
         204,  192,  181,  171,  161,  152,  144,  136,  128,  121,  114,  108,
         102,   96,   90,   85,   80,   76,   72,   68,   64,   60,   57,   54,
    },
    /* -8 */
    {
        1814, 1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016,  960,
         907,  856,  808,  762,  720,  678,  640,  604,  570,  538,  508,  480,
         453,  428,  404,  381,  360,  339,  320,  302,  285,  269,  254,  240,
         226,  214,  202,  190,  180,  170,  160,  151,  143,  135,  127,  120,
         113,  107,  101,   95,   90,   85,   80,   75,   71,   67,   63,   60,
    },
    /* -7 */
    {
        1800, 1700, 1604, 1514, 1430, 1350, 1272, 1202, 1134, 1070, 1010,  954,
         900,  850,  802,  757,  715,  675,  636,  601,  567,  535,  505,  477,
         450,  425,  401,  379,  357,  337,  318,  300,  284,  268,  253,  238,
         225,  212,  200,  189,  179,  169,  159,  150,  142,  134,  126,  119,
         112,  106,  100,   94,   89,   84,   79,   75,   71,   67,   63,   59,
    },
    /* -6 */
    {
        1788, 1688, 1592, 1504, 1418, 1340, 1264, 1194, 1126, 1064, 1004,  948,
         894,  844,  796,  752,  709,  670,  632,  597,  563,  532,  502,  474,
         447,  422,  398,  376,  355,  335,  316,  298,  282,  266,  251,  237,
         223,  211,  199,  188,  177,  167,  158,  149,  141,  133,  125,  118,
         111,  105,   99,   94,   88,   83,   79,   74,   70,   66,   62,   59,
    },
    /* -5 */
    {
        1774, 1676, 1582, 1492, 1408, 1330, 1256, 1184, 1118, 1056,  996,  940,
         887,  838,  791,  746,  704,  665,  628,  592,  559,  528,  498,  470,
         444,  419,  395,  373,  352,  332,  314,  296,  280,  264,  249,  235,
         222,  209,  198,  187,  176,  166,  157,  148,  140,  132,  125,  118,
         111,  104,   99,   93,   88,   83,   78,   74,   70,   66,   62,   59,
    },
    /* -4 */
    {
        1762, 1664, 1570, 1482, 1398, 1320, 1246, 1176, 1110, 1048,  988,  934,
         881,  832,  785,  741,  699,  660,  623,  588,  555,  524,  494,  467,
         441,  416,  392,  370,  350,  330,  312,  294,  278,  262,  247,  233,
         220,  208,  196,  185,  175,  165,  156,  147,  139,  131,  123,  117,
         110,  104,   98,   92,   87,   82,   78,   73,   69,   65,   61,   58,
    },
    /* -3 */
    {
        1750, 1652, 1558, 1472, 1388, 1310, 1238, 1168, 1102, 1040,  982,  926,
         875,  826,  779,  736,  694,  655,  619,  584,  551,  520,  491,  463,
         437,  413,  390,  368,  347,  328,  309,  292,  276,  260,  245,  232,
         219,  206,  195,  184,  174,  164,  155,  146,  138,  130,  123,  116,
         109,  103,   97,   92,   87,   82,   77,   73,   69,   65,   61,   58,
    },
    /* -2 */
    {
        1736, 1640, 1548, 1460, 1378, 1302, 1228, 1160, 1094, 1032,  974,  920,
         868,  820,  774,  730,  689,  651,  614,  580,  547,  516,  487,  460,
         434,  410,  387,  365,  345,  325,  307,  290,  274,  258,  244,  230,
         217,  205,  193,  183,  172,  163,  154,  145,  137,  129,  122,  115,
         108,  102,   96,   91,   86,   81,   77,   72,   68,   64,   61,   57,
    },
    /* -1 */
    {
        1724, 1628, 1536, 1450, 1368, 1292, 1220, 1150, 1086, 1026,  968,  914,
         862,  814,  768,  725,  684,  646,  610,  575,  543,  513,  484,  457,
         431,  407,  384,  363,  342,  323,  305,  288,  272,  256,  242,  228,
         216,  203,  192,  181,  171,  161,  152,  144,  136,  128,  121,  114,
         108,  101,   96,   90,   85,   80,   76,   72,   68,   64,   60,   57,
    },
};

/* sin(i * 2π / 64) * 255, zaokraglone.  Delta vibrato = wartosc * glebokosc / 128. */
static const int16_t k_vibrato[64] = {
    0, 25, 50, 74, 98, 120, 142, 162, 180, 197, 212, 225, 236, 244, 250, 254,
    255, 254, 250, 244, 236, 225, 212, 197, 180, 162, 142, 120, 98, 74, 50, 25,
    0, -25, -50, -74, -98, -120, -142, -162, -180, -197, -212, -225, -236, -244, -250, -254,
    -255, -254, -250, -244, -236, -225, -212, -197, -180, -162, -142, -120, -98, -74, -50, -25
};

/* Przyrost licznika EFx na takt, nibble 0..15.  Prog to 128, reszta ponad prog przepada. */
static const uint8_t k_invert[16] = {
    0, 5, 6, 7, 8, 10, 11, 13, 16, 19, 22, 26, 32, 43, 64, 128
};

typedef struct {
    const maktabasauti_host_t *host;
    const uint8_t *file;
    char title[21];
    char magic[5];
    inst_t inst[MOD_INST];
    uint8_t orders[128];
    const uint8_t *patterns;
    uint8_t song_length;
    uint8_t restart;
    uint16_t pattern_count;
    uint8_t channels;
    uint8_t instruments;
    uint8_t flt8;
    uint32_t rate;
    uint32_t clock;
    uint32_t order_pos;
    uint32_t row;
    uint32_t row_tick;
    uint32_t speed;
    uint32_t tempo;
    uint32_t tick_rem;
    uint32_t tick_left;
    uint32_t ticks_done;
    uint32_t tick_cap;
    uint64_t frame_pos;
    uint64_t total_frames;
    int have_tick;
    int ended;
    int16_t next_order;
    int16_t next_row;
    int8_t delay_notes;
    uint8_t loop_left;
    uint8_t revisit;
    uint32_t loop_row;
    uint32_t row_delay;
    uint8_t filter;
    uint8_t filter_on;
    uint8_t hold;
    uint8_t amiga_rc; /* 1: staly RC okolo 4,9 kHz, opcja mod.model a500 */
    uint8_t tracker; /* TRACK_PT11 domyslnie.  pt10, ust, st2, st9, tjc, st23 albo st25 */
    uint8_t clear_n; /* 2, 4 albo 0 bajtow prefiksu czytanych jako zero */
    uint32_t ust_tempo; /* BPM z bajtu 0x1D7, tylko 15-instrumentowy ust.  0: tempo 125. */
    double b0, b1, b2, a1, a2;
    double rc_a;
    double lx1, lx2, ly1, ly2;
    double rx1, rx2, ry1, ry2;
    double rca_x, rca_y, rcb_x, rcb_y;
    const char *err;
    uint8_t been[1024];
    voice_t voice[MOD_CH_MAX];
} mod_t;

typedef struct {
    uint8_t channels;
    uint8_t instruments;
    uint8_t flt8;
    uint16_t pattern_limit;
    uint16_t header;
    char magic[5];
} ident_t;

static uint16_t be16(const uint8_t *p) {
    return (uint16_t)(((unsigned)p[0] << 8) | p[1]);
}

static int tag_bytes(const uint8_t *m) {
    unsigned i;
    for (i = 0; i < 4u; i++) {
        unsigned c = m[i];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '!' || c == '&') continue;
        return 0;
    }
    return 1;
}

/* Sygnatury z publicznego opisu MOD.  xxCH/xxCN stoja na 10..99.
 * FLT6 nie jest formatem: autor StarTrekker opisal FLT4 i FLT8, a 6 kanalow to 6CHN. */

static int same4(const uint8_t *m, const char *s) {
    return memcmp(m, s, 4) == 0;
}

static int ident_magic(const uint8_t *m, ident_t *id) {
    unsigned ch;
    memset(id, 0, sizeof *id);
    id->instruments = 31;
    id->header = MOD_HEADER_31;
    id->pattern_limit = 256;
    memcpy(id->magic, m, 4u);
    id->magic[4] = 0;
    /* NoiseTracker i Startrekker 4-kanalowy zostaja przy limicie 64 patternow, jak M.K. */
    if (same4(m, "M.K.") || same4(m, "M&K!") || same4(m, "FLT4") || same4(m, "EXO4") || same4(m, "N.T.")) {
        id->channels = 4;
        id->pattern_limit = 64;
        return 1;
    }
    if (same4(m, "M!K!") || same4(m, "PATT") || same4(m, "NSMS") || same4(m, "LARD")) {
        id->channels = 4;
        return 1;
    }
    /* EXO8 i EX08 to ta sama para patternow co FLT8. */
    if (same4(m, "FLT8") || same4(m, "EXO8") || same4(m, "EX08")) {
        id->channels = 8;
        id->flt8 = 1;
        id->pattern_limit = 128;
        return 1;
    }
    if (same4(m, "CD81") || same4(m, "OCTA") || same4(m, "OKTA")) {
        id->channels = 8;
        return 1;
    }
    if (same4(m, "CD61")) {
        id->channels = 6;
        return 1;
    }
    if (same4(m, "FA04") || same4(m, "FA06") || same4(m, "FA08")) {
        id->channels = (uint8_t)(m[3] - '0');
        return 1;
    }
    if (m[0] == 'T' && m[1] == 'D' && m[2] == 'Z' && m[3] >= '1' && m[3] <= '9') {
        id->channels = (uint8_t)(m[3] - '0');
        return 1;
    }
    if (m[0] >= '1' && m[0] <= '9' && m[1] == 'C' && m[2] == 'H' && m[3] == 'N') {
        id->channels = (uint8_t)(m[0] - '0');
        return 1;
    }
    if (m[0] >= '0' && m[0] <= '9' && m[1] >= '0' && m[1] <= '9' && m[2] == 'C' && (m[3] == 'H' || m[3] == 'N')) {
        ch = (unsigned)(m[0] - '0') * 10u + (unsigned)(m[1] - '0');
        if (ch < 10u || ch > MOD_CH_MAX) return 0;
        id->channels = (uint8_t)ch;
        return 1;
    }
    return 0;
}

static void ident_15(ident_t *id) {
    memset(id, 0, sizeof *id);
    id->channels = 4;
    id->instruments = 15;
    id->pattern_limit = 64;
    id->header = MOD_HEADER_15;
    memcpy(id->magic, "15ST", 5u);
}

/* Opublikowany naglowek 15 instrumentow.  Glosnosc lezy w 0..64, a order wskazuje
 * pattern 0..63.  Sam bajt 470 w zakresie 1..128 trafia sie w plikach AAC, AIFF, MP3, VOC i WAV. */
static int header_15(const uint8_t *p) {
    unsigned i;
    for (i = 0; i < 15u; i++) {
        if (p[20u + i * 30u + 25u] > 64u) return 0;
    }
    for (i = 0; i < 128u; i++) {
        if (p[472u + i] >= 64u) return 0;
    }
    return 1;
}

static int recognize(const uint8_t *p, size_t n, ident_t *id) {
    if (n >= MOD_HEADER_31 && ident_magic(p + 1080, id)) return 1;
    if (n >= MOD_HEADER_31 && tag_bytes(p + 1080)) return 0;
    if (n < MOD_HEADER_15 || p[470] < 1u || p[470] > 128u) return 0;
    if (!header_15(p)) return 0;
    ident_15(id);
    return 1;
}

static const uint8_t *cell_at(const mod_t *m, unsigned pat, unsigned row, unsigned ch) {
    if (m->flt8) {
        unsigned stored = pat + (ch >> 2);
        return m->patterns + ((size_t)stored * 64u + row) * 16u + (size_t)(ch & 3u) * 4u;
    }
    return m->patterns + ((size_t)pat * 64u + row) * (size_t)m->channels * 4u + (size_t)ch * 4u;
}

static int16_t sat16(int32_t x) {
    if (x > 32767) return 32767;
    if (x < -32768) return -32768;
    return (int16_t)x;
}

static void playback_reset(mod_t *m) {
    uint32_t i;
    for (i = 0; i < MOD_CH_MAX; i++) {
        memset(&m->voice[i], 0, sizeof m->voice[i]);
        m->voice[i].pan = ((i & 3u) == 0u || (i & 3u) == 3u) ? 0 : 255;
    }
    m->order_pos = 0;
    m->row = 0;
    m->row_tick = 0;
    m->speed = m->tracker == TRACK_TJC ? 5u : 6u;
    m->tempo = m->ust_tempo ? m->ust_tempo : 125u;
    m->tick_rem = 0;
    m->tick_left = 0;
    m->ticks_done = 0;
    m->frame_pos = 0;
    m->have_tick = 0;
    m->ended = 0;
    m->next_order = -1;
    m->next_row = -1;
    m->delay_notes = -1;
    m->loop_left = 0;
    m->revisit = 0;
    m->loop_row = 0;
    m->row_delay = 0;
    m->filter = 0;
    m->filter_on = 0;
    m->hold = 0;
    m->lx1 = m->lx2 = m->ly1 = m->ly2 = 0;
    m->rx1 = m->rx2 = m->ry1 = m->ry2 = 0;
    m->rca_x = m->rca_y = m->rcb_x = m->rcb_y = 0;
    m->err = NULL;
    memset(m->been, 0, sizeof m->been);
    /* count_frames przechodzi caly utwor i ustawia bity, zanim zacznie sie prawdziwe granie. */
    for (i = 0; i < m->instruments; i++) {
        inst_t *in = &m->inst[i];
        if (in->flip && in->length) memset(in->flip, 0, ((size_t)in->length + 7u) / 8u);
    }
}

static int been_row(const mod_t *m, unsigned order, unsigned row) {
    unsigned bit = order * 64u + row;
    return (m->been[bit >> 3] >> (bit & 7u)) & 1;
}

static void mark_row(mod_t *m, unsigned order, unsigned row) {
    unsigned bit = order * 64u + row;
    m->been[bit >> 3] |= (uint8_t)(1u << (bit & 7u));
}

static uint32_t tick_frames(mod_t *m) {
    uint64_t acc = (uint64_t)m->tick_rem + (uint64_t)m->rate * 5u;
    uint32_t den = m->tempo * 2u;
    m->tick_rem = (uint32_t)(acc % den);
    return (uint32_t)(acc / den);
}

static int held_waiting(const voice_t *v) {
    return v->held != v->sample || v->held_len != v->length
        || v->held_loop_start != v->loop_start || v->held_loop_len != v->loop_len;
}

/* Koniec przebiegu bierze petle zatrzasku.  Bez petli kanal milknie i nie startuje jednorazowej probki. */
static void adopt_held(voice_t *v, uint32_t overshoot) {
    v->sample = v->held;
    v->length = v->held_len;
    v->loop_start = v->held_loop_start;
    v->loop_len = v->held_loop_len;
    v->loop_once = 0;
    if (v->loop_len) {
        v->idx = v->loop_start + overshoot % v->loop_len;
        return;
    }
    v->idx = v->length;
    v->frac = 0;
}

static void settle(voice_t *v) {
    uint32_t end;
    /* Petla od zera: ProTracker gra cala probke raz, dopiero potem wchodzi w petle. */
    if (v->loop_len && v->loop_once) {
        if (v->idx < v->length) return;
        v->loop_once = 0;
        if (held_waiting(v)) adopt_held(v, v->idx - v->length);
        else v->idx = (v->idx - v->length) % v->loop_len;
        return;
    }
    if (v->loop_len) {
        end = v->loop_start + v->loop_len;
        if (v->idx < end) return;
        if (held_waiting(v)) adopt_held(v, v->idx - end);
        else v->idx = v->loop_start + (v->idx - v->loop_start) % v->loop_len;
        return;
    }
    if (!v->sample) {
        v->idx = 0;
        v->frac = 0;
        return;
    }
    if (v->idx < v->length) return;
    if (held_waiting(v)) adopt_held(v, v->idx - v->length);
    else {
        v->idx = v->length;
        v->frac = 0;
    }
}

static void use_period(voice_t *v, uint16_t period) {
    if (v->play_period && period && v->play_period != period)
        v->frac = v->frac * period / v->play_period;
    v->play_period = period;
}

static int tune_index(int ft) {
    if (ft < -8) ft = -8;
    if (ft > 7) ft = 7;
    return ft < 0 ? ft + 16 : ft;
}

static uint16_t column_period(int note, int ft) {
    if (note < 0) note = 0;
    if (note >= MOD_NOTES) note = MOD_NOTES - 1;
    return k_periods[tune_index(ft)][note];
}

static int nearest_note(unsigned period, int ft) {
    unsigned i, best = 0, gap = 65535u;
    int col = tune_index(ft);
    for (i = 0; i < MOD_NOTES; i++) {
        unsigned tp = k_periods[col][i];
        unsigned d = period > tp ? period - tp : tp - period;
        if (d < gap) { gap = d; best = i; }
    }
    return (int)best;
}

/* Zapisany okres jest z tabeli finetune 0.  Niezerowe strojenie bierze ten sam numer nuty ze swojej kolumny. */
static uint16_t tuned_period(unsigned period, int ft) {
    if (!period) return 0;
    if (!ft) return (uint16_t)period;
    return column_period(nearest_note(period, 0), ft);
}

static uint16_t arpeggio_period(unsigned base, unsigned semis, int ft) {
    int note = nearest_note(base, ft) + (int)semis;
    if (note < 0) note = 0;
    if (note >= MOD_NOTES) note = MOD_NOTES - 1;
    return column_period(note, ft);
}

/* Rampa tremolo w ProTrackerze patrzy na bit polowy pozycji vibrato.  Delta = (mag * glebokosc) >> 3. */
static int32_t tremolo_ramp(const voice_t *v) {
    int t = (int)v->trem_pos;
    int mag = (((int)v->vib_pos < 32 ? t : -t) & 31);
    int delta = (mag * (int)v->trem_depth) >> 3;
    if (t >= 32) delta = -delta;
    return delta;
}

static int16_t wave_sample(unsigned wave, unsigned pos) {
    pos &= 63u;
    if ((wave & 3u) == 1u) return (int16_t)(255 - (int)pos * 8);
    /* Fala 3 to ten sam kwadrat.  ProTracker nie losuje, wiec generatora nie ma. */
    if ((wave & 3u) >= 2u) return pos < 32u ? 255 : -255;
    return k_vibrato[pos];
}

static void voice_advance(voice_t *v, uint32_t rate, uint32_t clock) {
    uint64_t den, idx;
    if (!v->play_period || !rate || !clock) return;
    den = (uint64_t)v->play_period * rate;
    v->frac += clock;
    idx = (uint64_t)v->idx + v->frac / den;
    v->frac %= den;
    v->idx = idx > 0xffffffffu ? 0xffffffffu : (uint32_t)idx;
    settle(v);
}

static int8_t nibble_tune(unsigned nib) {
    nib &= 0x0Fu;
    return nib < 8u ? (int8_t)nib : (int8_t)((int)nib - 16);
}

static int st_family(uint8_t tracker);

static void latch_instrument(mod_t *m, voice_t *v, unsigned sample) {
    const inst_t *in;
    if (sample < 1u || sample > m->instruments) return;
    /* Inna probka ma inna petle.  Licznik EFx zostaje, zeruje sie tylko pozycja.
     * Dzwiek zostaje przy starej probce, az skonczy sie jej przebieg albo nuta ja wymieni.
     * Nowy numer w pt10 ustawia kursor Funk Repeat i prawdziwy poczatek petli w to samo miejsce. */
    in = &m->inst[sample - 1u];
    if (v->inst != (uint8_t)sample) {
        v->inv_pos = 0;
        if (m->tracker == TRACK_PT10) {
            v->wave_pos = in->loop_start;
            v->real_loop = in->loop_start;
        }
    }
    v->held = in->data;
    v->held_len = in->length;
    v->held_loop_start = in->loop_start;
    v->held_loop_len = in->loop_len;
    v->volume = in->volume;
    v->inst = (uint8_t)sample;
    /* Ultimate Soundtracker nie uzywa bajtu finetune.  Tag i tak czyta bajt z pliku. */
    /* SoundTracker nie stroi z bajtu finetune.  Tag i tak czyta bajt z pliku.
     * st25 tez: to NoiseTracker 1.1 bez tego bajtu w odtwarzaniu. */
    v->tune = (st_family(m->tracker) || m->tracker == TRACK_ST25) ? 0 : in->finetune;
    v->start = 0;
}

/* Grupa 1 Ultimate Soundtrackera.  1xy to arpeggio.  2xy to porta w gore o y albo w dol o x.
 * Oba nibble naraz albo oba zera nie sa opisane: efekt znika.  Reszta numerow tez. */
static void ust_remap(unsigned *effect, unsigned *param) {
    unsigned hi, lo;
    if (*effect == 1u) {
        *effect = 0u;
        return;
    }
    if (*effect == 2u) {
        hi = *param >> 4;
        lo = *param & 0x0Fu;
        if (hi == 0u && lo != 0u) { *effect = 1u; *param = lo; return; }
        if (lo == 0u && hi != 0u) { *effect = 2u; *param = hi; return; }
    }
    *effect = 0u;
    *param = 0u;
}

static int st_family(uint8_t tracker) {
    return tracker == TRACK_UST || tracker == TRACK_ST2 || tracker == TRACK_ST9
        || tracker == TRACK_TJC || tracker == TRACK_ST23;
}

/* II-VI.  0xy, 1xx i 2xx jak w ProTrackerze.  Dxy slajduje jak Axy.  Exy zapamietuje ten slajd
 * i doklada go do kolejnych komend, az parametr wyniesie 00.  F bierze tylko dolny nibble.
 * Parametr 00 gasi slajd.  Poza C00 taka komenda nic wiecej nie robi. */
static void st2_row(voice_t *v, unsigned *effect, unsigned *param) {
    unsigned e = *effect, p = *param;
    if (p == 0u) {
        v->auto_on = 0;
        if (e != 0x0Cu) {
            *effect = 0u;
            *param = 0u;
        }
        return;
    }
    if (e == 0x0Eu) {
        v->auto_slide = (uint8_t)p;
        v->auto_on = 1u;
        v->vol_slide = (uint8_t)p;
        *effect = 0x0Au;
        return;
    }
    if (e == 0x0Du) {
        *effect = 0x0Au;
        return;
    }
    if (e == 0x00u || e == 0x01u || e == 0x02u || e == 0x0Cu || e == 0x0Fu) {
        if (v->auto_on) v->vol_slide = v->auto_slide;
        return;
    }
    *effect = 0u;
    *param = 0u;
    if (v->auto_on) v->vol_slide = v->auto_slide;
}

/* IX i Master.  Dxy znika: kazde D lamanie patternu na wiersz 0, parametr jest ignorowany.
 * Zostaja 0xy, 1xx, 2xx, Bxx, Cxx, E0x i F z dolnego nibble. */
static void st9_row(unsigned *effect, unsigned *param) {
    unsigned e = *effect;
    if (e == 0x0Du) {
        *param = 0u;
        return;
    }
    if (e == 0x0Eu) {
        if ((*param >> 4) != 0u) {
            *effect = 0u;
            *param = 0u;
        }
        return;
    }
    if (e == 0x00u || e == 0x01u || e == 0x02u || e == 0x0Bu || e == 0x0Cu || e == 0x0Fu) return;
    *effect = 0u;
    *param = 0u;
}

/* Jungle Command, SoundTracker II.  Glos N zawsze steruje glosem N+1 i sam milczy.
 * 3..5 to arpeggio modulatora, 6..8 porta w gore, 9..B porta w dol.  D sklada oba nibble.
 * E pamieta slajd, az info wyniesie 0.  Parametr 0 nie powtarza pamieci portamento.  F nie istnieje. */
static void tjc_row(voice_t *v, unsigned *effect, unsigned *param) {
    unsigned e = *effect;
    unsigned p = *param;
    v->mod_vol = 0;
    v->mod_per = 0;
    v->mod_silent = 0;
    if (p == 0u) v->auto_on = 0;
    if (e >= 3u && e <= 0x0Bu) {
        v->mod_silent = 1u;
        if (e == 3u || e == 5u || e == 6u || e == 8u || e == 9u || e == 0x0Bu) v->mod_vol = 1u;
        if (e == 4u || e == 5u || e == 7u || e == 8u || e == 0x0Au || e == 0x0Bu) v->mod_per = 1u;
        if (e <= 5u || p == 0u) {
            *effect = 0u;
            if (e >= 6u) *param = 0u;
        } else if (e <= 8u) *effect = 1u;
        else *effect = 2u;
        if (v->auto_on) v->vol_slide = v->auto_slide;
        return;
    }
    if (e == 0x0Eu) {
        if (p) {
            v->auto_slide = (uint8_t)p;
            v->auto_on = 1u;
            v->vol_slide = (uint8_t)p;
            *effect = 0x0Au;
        } else {
            *effect = 0u;
            *param = 0u;
        }
        return;
    }
    if (e == 0x0Du) {
        *effect = 0x0Au;
        return;
    }
    if (e == 0x0Cu) return;
    if (e == 0x01u || e == 0x02u) {
        if (p == 0u) {
            *effect = 0u;
            *param = 0u;
            return;
        }
        if (v->auto_on) v->vol_slide = v->auto_slide;
        return;
    }
    if (e == 0x00u) {
        if (v->auto_on) v->vol_slide = v->auto_slide;
        return;
    }
    *effect = 0u;
    *param = 0u;
    if (v->auto_on) v->vol_slide = v->auto_slide;
}

/* DOC Soundtracker 2.3 i 2.4.  Zestaw Master Soundtracker plus Axy.  D ignoruje parametr. */
static void st23_row(unsigned *effect, unsigned *param) {
    unsigned e = *effect;
    if (e == 0x0Du) {
        *param = 0u;
        return;
    }
    if (e == 0x0Eu) {
        if ((*param >> 4) != 0u) {
            *effect = 0u;
            *param = 0u;
        }
        return;
    }
    if (e == 0x00u || e == 0x01u || e == 0x02u || e == 0x0Au || e == 0x0Bu || e == 0x0Cu || e == 0x0Fu) return;
    *effect = 0u;
    *param = 0u;
}

/* Soundtracker 2.5, komendy NoiseTrackera 1.1.  Dxx zostaje dziesietne.
 * Glebia vibrato zostaje przy wzorze ProTrackera: starszy wspolczynnik 1.1 nie byl opublikowany. */
static void st25_row(unsigned *effect, unsigned *param) {
    unsigned e = *effect;
    if (e == 0x0Eu) {
        if ((*param >> 4) != 0u) {
            *effect = 0u;
            *param = 0u;
        }
        return;
    }
    if (e == 0x00u || e == 0x01u || e == 0x02u || e == 0x03u || e == 0x04u
        || e == 0x0Au || e == 0x0Bu || e == 0x0Cu || e == 0x0Du || e == 0x0Fu) return;
    *effect = 0u;
    *param = 0u;
}

static void arm_held(voice_t *v) {
    v->sample = v->held;
    v->length = v->held_len;
    v->loop_start = v->held_loop_start;
    v->loop_len = v->held_loop_len;
    /* Petla od bajtu 0 i krotsza niz probka: cala probka gra raz.  Adopt wchodzi od razu w petle. */
    v->loop_once = v->loop_start == 0u && v->loop_len != 0u && v->loop_len < v->length;
}

/* Prawdziwy trigger zeruje faze i pozycje fal, chyba ze bit +4 fali tego zabrania.
 * 3xx/5xx przy juz grajacym glosie tylko zapamietuje cel.  9xx skacze wylacznie wtedy. */
static void trigger_note(voice_t *v, unsigned period, unsigned effect) {
    uint32_t at;
    uint16_t tp;
    if (!period) return;
    tp = tuned_period(period, v->tune);
    if ((effect == 0x3u || effect == 0x5u) && v->period) {
        v->target = tp;
        return;
    }
    arm_held(v);
    at = v->start;
    /* 9xx dodaje offset do zagrania i drugi raz do zatrzasku.  Nastepna nuta bez instrumentu startuje z 2*offset. */
    if (effect == 0x9u && v->offset) {
        uint32_t add = (uint32_t)v->offset * 256u;
        at += add;
        v->start = at + add;
    }
    v->period = tp;
    v->target = tp;
    v->note_period = tp;
    v->mod_idx = 0;
    v->mod_frac = 0;
    v->frac = 0;
    v->play_period = 0;
    if ((v->vib_wave & 4u) == 0u) v->vib_pos = 0;
    if ((v->trem_wave & 4u) == 0u) v->trem_pos = 0;
    if (!v->loop_len && at >= v->length) v->idx = v->length;
    else v->idx = at;
}

static void apply_row(mod_t *m, uint32_t *tempo_after) {
    unsigned ch;
    for (ch = 0; ch < m->channels; ch++) {
        const uint8_t *c = cell_at(m, m->orders[m->order_pos], m->row, ch);
        unsigned sample = (c[0] & 0xF0u) | (c[2] >> 4);
        unsigned period = ((unsigned)(c[0] & 0x0Fu) << 8) | c[1];
        unsigned effect = c[2] & 0x0Fu;
        unsigned param = c[3];
        unsigned ex, xn;
        voice_t *v = &m->voice[ch];
        if (m->tracker == TRACK_UST) ust_remap(&effect, &param);
        else if (m->tracker == TRACK_ST2) st2_row(v, &effect, &param);
        else if (m->tracker == TRACK_ST9) st9_row(&effect, &param);
        else if (m->tracker == TRACK_TJC) tjc_row(v, &effect, &param);
        else if (m->tracker == TRACK_ST23) st23_row(&effect, &param);
        else if (m->tracker == TRACK_ST25) st25_row(&effect, &param);
        ex = param >> 4;
        xn = param & 0x0Fu;
        v->effect = (uint8_t)effect;
        v->param = (uint8_t)param;
        /* EDx z niezerowym x zatrzaskuje instrument od razu, a nute odpala pozniej.  9xx tej komorki nie gra. */
        if (effect == 0x0Eu && ex == 0xDu && xn) {
            latch_instrument(m, v, sample);
            v->delay_tick = (uint8_t)xn;
            v->delay_sample = (uint8_t)sample;
            v->delay_period = (uint16_t)period;
            continue;
        }
        v->delay_tick = 0;
        if (effect == 0x1u && param) v->porta_up = (uint8_t)param;
        if (effect == 0x2u && param) v->porta_down = (uint8_t)param;
        if (effect == 0x3u && param) v->tone_speed = (uint8_t)param;
        if (effect == 0x4u) {
            if (param >> 4) v->vib_speed = (uint8_t)(param >> 4);
            if (param & 0x0Fu) v->vib_depth = (uint8_t)(param & 0x0Fu);
        }
        if (effect == 0x7u) {
            if (param >> 4) v->trem_speed = (uint8_t)(param >> 4);
            if (param & 0x0Fu) v->trem_depth = (uint8_t)(param & 0x0Fu);
        }
        if ((effect == 0x5u || effect == 0x6u || effect == 0xAu) && param) v->vol_slide = (uint8_t)param;
        if (effect == 0x9u && param) v->offset = (uint8_t)param;
        if (effect == 0x0Eu && ex == 0x1u && xn) v->fine_up = (uint8_t)xn;
        if (effect == 0x0Eu && ex == 0x2u && xn) v->fine_down = (uint8_t)xn;
        if (effect == 0x0Eu && ex == 0x9u && xn) v->retrig = (uint8_t)xn;
        if (effect == 0x0Eu && ex == 0xAu && xn) v->fine_vol_up = (uint8_t)xn;
        if (effect == 0x0Eu && ex == 0xBu && xn) v->fine_vol_down = (uint8_t)xn;
        if (effect == 0x0Eu && ex == 0x0u) {
            int on = xn == 0u;
            if (on && !m->filter_on) {
                m->lx1 = m->lx2 = m->ly1 = m->ly2 = 0;
                m->rx1 = m->rx2 = m->ry1 = m->ry2 = 0;
            }
            m->filter_on = (uint8_t)on;
            m->filter = (uint8_t)xn;
        }
        /* Fala musi byc ustawiona przed triggerem, zeby bit +4 zdazyl zatrzymac reset fazy. */
        if (effect == 0x0Eu && ex == 0x3u) v->gliss = xn ? 1u : 0u;
        if (effect == 0x0Eu && ex == 0x4u) v->vib_wave = (uint8_t)xn;
        if (effect == 0x0Eu && ex == 0x7u) v->trem_wave = (uint8_t)xn;
        latch_instrument(m, v, sample);
        /* E5x tej samej komorki stroi nute, zanim okres zostanie przepisany z tabeli. */
        if (effect == 0x0Eu && ex == 0x5u) v->tune = nibble_tune(xn);
        trigger_note(v, period, effect);
        if (effect == 0x0Eu && ex == 0x6u) {
            if (!xn) m->loop_row = m->row;
            else {
                if (!m->loop_left) m->loop_left = (uint8_t)xn;
                else m->loop_left--;
                if (m->loop_left) {
                    m->next_order = (int16_t)m->order_pos;
                    m->next_row = (int16_t)m->loop_row;
                    m->revisit = 1;
                }
            }
        }
        if (effect == 0x0Eu && ex == 0x8u) v->pan = (uint8_t)(xn * 17u);
        /* pt10 ustawia predkosc dopiero po pierwszym kroku Funk Repeat, ze stara wartoscia. */
        if (effect == 0x0Eu && ex == 0x0Fu && m->tracker != TRACK_PT10) {
            if (!xn) {
                v->inv_speed = 0;
                v->inv_acc = 0;
                v->inv_pos = 0;
            } else v->inv_speed = k_invert[xn];
        }
        if (effect == 0x0Eu && ex == 0xEu) m->delay_notes = (int8_t)xn;
        if (effect == 0x8u) v->pan = (uint8_t)param;
        if (effect == 0x0Bu) {
            m->next_order = (int16_t)param;
            m->revisit = 0;
        }
        if (effect == 0x0Du) {
            unsigned brow = (param >> 4) * 10u + (param & 0x0Fu);
            if (brow > 63u) brow = 0u;
            m->next_row = (int16_t)brow;
            m->revisit = 0;
        }
        if (effect == 0x0Cu) v->volume = param > 64u ? 64u : (uint8_t)param;
        if (effect == 0x0Fu) {
            if (m->tracker == TRACK_ST2 || m->tracker == TRACK_ST9 || m->tracker == TRACK_ST23) {
                unsigned sp = param & 0x0Fu;
                if (sp >= 1u && sp <= 15u) m->speed = sp;
            } else if (m->tracker == TRACK_ST25) {
                unsigned sp = param & 31u;
                if (sp) m->speed = sp;
            } else if (!param) m->hold = 1;
            else if (m->instruments == 15u || param < 32u) m->speed = param;
            else *tempo_after = param;
        }
    }
}

static void clamp_volume(voice_t *v, int delta) {
    int vol = (int)v->volume + delta;
    if (vol < 0) vol = 0;
    if (vol > 64) vol = 64;
    v->volume = (uint8_t)vol;
}

static void slide_volume(voice_t *v) {
    unsigned up = v->vol_slide >> 4, down = v->vol_slide & 0x0Fu;
    if (!v->vol_slide) return;
    if (up) clamp_volume(v, (int)up);
    else clamp_volume(v, -(int)down);
}

/* Jungle Command: oba nibble sa zdefiniowane.  Roznica, potem jeden zacisk 0..64. */
static void tjc_slide(voice_t *v) {
    unsigned up = v->vol_slide >> 4, down = v->vol_slide & 0x0Fu;
    if (!v->vol_slide) return;
    clamp_volume(v, (int)up - (int)down);
}

static void porta_up(voice_t *v) {
    unsigned p = v->period, step = v->porta_up;
    if (!step || p <= 113u) return;
    v->period = (uint16_t)(p - 113u > step ? p - step : 113u);
}

static void porta_down(voice_t *v) {
    unsigned p = v->period, step = v->porta_down;
    if (!step || !p || p >= 856u) return;
    v->period = (uint16_t)(856u - p > step ? p + step : 856u);
}

static void fine_porta_up(voice_t *v) {
    unsigned p = v->period, step = v->fine_up;
    if (!step || p <= 113u) return;
    v->period = (uint16_t)(p - 113u > step ? p - step : 113u);
}

static void fine_porta_down(voice_t *v) {
    unsigned p = v->period, step = v->fine_down;
    if (!step || !p || p >= 856u) return;
    v->period = (uint16_t)(856u - p > step ? p + step : 856u);
}

static void gliss_snap(voice_t *v) {
    if (!v->gliss || !v->period) return;
    v->period = column_period(nearest_note(v->period, v->tune), v->tune);
}

static void tone_porta(voice_t *v) {
    unsigned p = v->period, goal = v->target, step = v->tone_speed;
    if (!step || !p || !goal || p == goal) return;
    if (p < goal) v->period = (uint16_t)(goal - p > step ? p + step : goal);
    else v->period = (uint16_t)(p - goal > step ? p - step : goal);
}

static void shape_tick(mod_t *m) {
    unsigned ch;
    for (ch = 0; ch < m->channels; ch++) {
        voice_t *v = &m->voice[ch];
        unsigned fx = v->effect, pa = v->param;
        unsigned ex = pa >> 4, xn = pa & 0x0Fu;
        v->tremolo = 0;
        if (m->row_tick == 0u && fx == 0x0Eu) {
            if (ex == 0x1u) fine_porta_up(v);
            else if (ex == 0x2u) fine_porta_down(v);
            else if (ex == 0xAu) clamp_volume(v, (int)v->fine_vol_up);
            else if (ex == 0xBu) clamp_volume(v, -(int)v->fine_vol_down);
        }
        /* Jungle Command rusza porta i slajd w kazdym z pieciu przejsc, takze w pierwszym. */
        if (m->tracker == TRACK_TJC) {
            if (fx == 0x1u) porta_up(v);
            else if (fx == 0x2u) porta_down(v);
            if (fx == 0x0Au || v->auto_on) tjc_slide(v);
        } else if (m->row_tick > 0u) {
            if (fx == 0x1u) porta_up(v);
            else if (fx == 0x2u) porta_down(v);
            else if (fx == 0x3u || fx == 0x5u) {
                tone_porta(v);
                gliss_snap(v);
            }
            if (fx == 0x5u || fx == 0x6u || fx == 0xAu) slide_volume(v);
            else if (v->auto_on) slide_volume(v);
        }
        if (fx == 0x0Eu && ex == 0xCu && m->row_tick == xn) v->volume = 0;
        /* E9x x=0 nic nie robi.  x>0 wraca na zatrzasniety poczatek, takze w takcie 0 i bez nuty. */
        if (fx == 0x0Eu && ex == 0x9u && xn && m->row_tick % xn == 0u) {
            v->idx = v->start;
            v->frac = 0;
        }
        if ((fx == 0x4u || fx == 0x6u) && m->row_tick > 0u && v->period) {
            int32_t waved = (int32_t)wave_sample(v->vib_wave, v->vib_pos) * (int32_t)v->vib_depth / 128;
            int32_t sounded = (int32_t)v->period + waved;
            if (sounded < 1) sounded = 1;
            use_period(v, (uint16_t)sounded);
            v->vib_pos = (uint8_t)((v->vib_pos + v->vib_speed) & 63u);
        } else if (fx == 0x7u && m->row_tick > 0u) {
            int32_t delta = (v->trem_wave & 3u) == 1u
                ? tremolo_ramp(v)
                : (int32_t)wave_sample(v->trem_wave, v->trem_pos) * (int32_t)v->trem_depth / 64;
            v->tremolo = (int16_t)delta;
            v->trem_pos = (uint8_t)((v->trem_pos + v->trem_speed) & 63u);
            use_period(v, v->period);
        } else if (fx == 0u && pa && v->period) {
            unsigned semis = 0, base = v->period;
            if (m->tracker == TRACK_TJC) {
                unsigned step = m->row_tick % 5u;
                if (step == 0u || step == 3u) semis = pa >> 4;
                else if (step == 1u || step == 4u) semis = pa & 0x0Fu;
            } else if (m->tracker == TRACK_ST23 && m->row_tick >= 6u) {
                base = v->period;
            } else {
                unsigned part = m->row_tick % 3u;
                if (part == 1u) semis = pa >> 4;
                else if (part == 2u) semis = pa & 0x0Fu;
                if (m->tracker == TRACK_ST23 && v->note_period) base = v->note_period;
            }
            use_period(v, semis ? arpeggio_period(base, semis, v->tune) : base);
        } else use_period(v, v->period);
    }
}

static void fire_delay(mod_t *m) {
    unsigned ch;
    for (ch = 0; ch < m->channels; ch++) {
        voice_t *v = &m->voice[ch];
        if (!v->delay_tick || m->row_tick != v->delay_tick) continue;
        latch_instrument(m, v, v->delay_sample);
        trigger_note(v, v->delay_period, 0);
        v->delay_tick = 0;
    }
}

static void flip_xor(inst_t *in, uint32_t idx) {
    if (!in->flip || idx >= in->length) return;
    in->flip[idx >> 3] ^= (uint8_t)(1u << (idx & 7u));
}

/* Po apply_row, takze w takcie, ktory wlasnie wlaczyl EFx, i dalej az predkosc spadnie do zera.
 * Najpierw odwraca bajt, potem przesuwa pozycje.  43 skacze 86, potem 129: reszta przepada. */
static void invert_step(mod_t *m, voice_t *v) {
    inst_t *in;
    uint32_t at;
    if (!v->inv_speed || v->inst < 1u || v->inst > m->instruments) return;
    in = &m->inst[v->inst - 1u];
    if (in->loop_len < 2u) return;
    v->inv_acc = (uint16_t)(v->inv_acc + v->inv_speed);
    if (v->inv_acc < 128u) return;
    v->inv_acc = 0;
    if (v->inv_pos >= in->loop_len) v->inv_pos = 0;
    at = in->loop_start + v->inv_pos;
    if (at < in->length) flip_xor(in, at);
    v->inv_pos++;
    if (v->inv_pos >= in->loop_len) v->inv_pos = 0;
}

/* Okno slyszane przez petle.  first_pass przywraca przejscie calej probki, gdy petla zaczyna sie od zera. */
static void funk_hear(voice_t *v, uint32_t at, int first_pass) {
    v->loop_start = at;
    v->held_loop_start = at;
    v->loop_once = (first_pass && at == 0u && v->loop_len != 0u && v->loop_len < v->length) ? 1u : 0u;
}

/* Jeden krok Funk Repeat.  Licznik od 128 wraca do zera (reszta przepada) i okno petli skacze o jej dlugosc.
 * Za koncem probki kursor wraca na prawdziwy poczatek petli.  Probka nie jest zmieniana. */
static void funk_step(voice_t *v) {
    uint32_t loop_len, limit, candidate;
    if (!v->inv_speed || !v->sample) return;
    loop_len = v->loop_len;
    if (loop_len < 2u || v->length < loop_len) return;
    v->inv_acc = (uint16_t)(v->inv_acc + v->inv_speed);
    if (v->inv_acc < 128u) return;
    v->inv_acc = 0;
    limit = v->length - loop_len;
    candidate = v->wave_pos > 0xffffffffu - loop_len ? 0xffffffffu : v->wave_pos + loop_len;
    v->wave_pos = candidate <= limit ? candidate : v->real_loop;
    funk_hear(v, v->wave_pos, 0);
}

/* Kazdy takt: krok stara predkoscia.  W takcie 0 EFx zmienia predkosc i, gdy jest niezerowa, robi drugi krok.
 * EF0 gasi predkosc i nie zeruje licznika.  9xx doklada jeszcze jeden krok.  SetDMA cofa slyszane okno
 * na prawdziwy poczatek petli, ale tylko w takcie 0 wiersza bez opoznienia patternu.  Kursor zostaje. */
static void pt10_tick(mod_t *m, int pattern_delay) {
    unsigned ch;
    int snap = m->row_tick == 0u && !pattern_delay;
    for (ch = 0; ch < m->channels; ch++) {
        voice_t *v = &m->voice[ch];
        unsigned fx, pa, ex, xn;
        funk_step(v);
        if (m->row_tick != 0u) continue;
        fx = v->effect;
        pa = v->param;
        ex = pa >> 4;
        xn = pa & 0x0Fu;
        if (fx == 0x0Eu && ex == 0x0Fu) {
            if (!xn) v->inv_speed = 0;
            else {
                v->inv_speed = k_invert[xn];
                funk_step(v);
            }
        }
        if (fx == 0x09u) funk_step(v);
        if (snap && v->loop_len >= 2u) funk_hear(v, v->real_loop, 1);
    }
}

static int channel_loops(const mod_t *m) {
    unsigned ch;
    for (ch = 0; ch < m->channels; ch++) {
        const voice_t *v = &m->voice[ch];
        if (v->mod_silent) continue;
        if (v->sample && v->loop_len) return 1;
        if (v->held && v->held_loop_len && held_waiting(v)) return 1;
    }
    return 0;
}

static int channel_audible(const mod_t *m) {
    unsigned ch;
    for (ch = 0; ch < m->channels; ch++) {
        const voice_t *v = &m->voice[ch];
        if (v->mod_silent) continue;
        if (!v->play_period || !v->sample) continue;
        if (v->loop_len) return 1;
        if (v->idx < v->length) return 1;
        if (held_waiting(v) && v->held && v->held_loop_len) return 1;
    }
    return 0;
}

static void advance_samples(mod_t *m, uint32_t frames) {
    unsigned ch;
    for (ch = 0; ch < m->channels; ch++) {
        voice_t *v = &m->voice[ch];
        uint64_t den, add;
        if (!frames || !v->play_period || !m->rate || !m->clock) continue;
        den = (uint64_t)v->play_period * m->rate;
        v->frac += (uint64_t)m->clock * frames;
        add = v->frac / den;
        v->frac %= den;
        if (add > 0xffffffffu - v->idx) v->idx = 0xffffffffu;
        else v->idx += (uint32_t)add;
        settle(v);
    }
}

static int begin_tick(mod_t *m) {
    uint32_t tempo_after;
    unsigned ch;
    int pattern_delay = 0;
    if (m->ended) return 0;
    /* F00 zatrzymuje sekwencer po pierwszym takcie.  Petla gra dalej, jednorazowa konczy sie sama. */
    if (m->hold) {
        if (!channel_audible(m)) {
            m->ended = 1;
            return 0;
        }
        m->tick_left = tick_frames(m);
        m->have_tick = 1;
        return 1;
    }
    if (m->ticks_done >= m->tick_cap) {
        m->ended = 1;
        m->err = "przekroczony limit taktow";
        return 0;
    }
    m->ticks_done++;
    tempo_after = m->tempo;
    if (m->row_tick == 0u) {
        mark_row(m, m->order_pos, m->row);
        m->delay_notes = -1;
        apply_row(m, &tempo_after);
        pattern_delay = m->delay_notes >= 0;
        if (pattern_delay) m->row_delay = (uint32_t)m->delay_notes * m->speed;
        m->delay_notes = -1;
    }
    fire_delay(m);
    shape_tick(m);
    if (m->tracker == TRACK_PT10) pt10_tick(m, pattern_delay);
    else for (ch = 0; ch < m->channels; ch++) invert_step(m, &m->voice[ch]);
    m->tick_left = tick_frames(m);
    m->tempo = tempo_after;
    m->have_tick = 1;
    return 1;
}

/* 0x78 i 0x7F oraz wartosc spoza listy nie sa pozycja.  Wtedy NoiseTracker wraca na poczatek, a been[] ucina powrot. */
static int restart_order(const mod_t *m, unsigned *dest) {
    unsigned r = m->restart;
    if (r == 0x78u || r == 0x7Fu || r >= m->song_length) {
        *dest = 0;
        return 1;
    }
    *dest = r;
    return 1;
}

static void end_tick(mod_t *m) {
    int revisit;
    int from_b;
    unsigned ch;
    m->have_tick = 0;
    m->tick_left = 0;
    if (m->hold) return;
    m->row_tick++;
    if (m->row_tick < m->speed + m->row_delay) return;
    /* Opznienie dluzsze niz wiersz nie odpala nuty, ale zostawia jej okres na nastepny wiersz. */
    for (ch = 0; ch < m->channels; ch++) {
        voice_t *v = &m->voice[ch];
        if (!v->delay_tick) continue;
        if (v->delay_period) v->period = v->delay_period;
        v->delay_tick = 0;
    }
    m->row_tick = 0;
    m->row_delay = 0;
    revisit = m->revisit;
    m->revisit = 0;
    from_b = m->next_order >= 0;
    if (from_b || m->next_row >= 0) {
        unsigned dest_order = from_b ? (unsigned)m->next_order : m->order_pos + 1u;
        unsigned dest_row = m->next_row >= 0 ? (unsigned)m->next_row : 0u;
        m->next_order = -1;
        m->next_row = -1;
        /* Bxx za koniec listy wraca na pozycje 0.  Samo spadniecie z konca listy idzie w restart. */
        if (from_b && dest_order >= m->song_length) dest_order = 0;
        if (dest_order >= m->song_length || (!revisit && been_row(m, dest_order, dest_row))) {
            m->ended = 1;
            return;
        }
        if (dest_order != m->order_pos) {
            m->loop_row = 0;
            m->loop_left = 0;
        }
        m->order_pos = dest_order;
        m->row = dest_row;
        return;
    }
    m->row++;
    if (m->row < 64u) return;
    m->row = 0;
    m->loop_row = 0;
    m->loop_left = 0;
    m->order_pos++;
    if (m->order_pos >= m->song_length) {
        unsigned dest;
        if (restart_order(m, &dest) && !been_row(m, dest, 0)) {
            m->order_pos = dest;
            m->row = 0;
            return;
        }
        m->ended = 1;
    }
}

static int count_frames(mod_t *m, uint64_t *out) {
    uint64_t n = 0;
    playback_reset(m);
    for (;;) {
        if (!begin_tick(m)) break;
        n += m->tick_left;
        advance_samples(m, m->tick_left);
        if (m->hold) {
            if (channel_loops(m)) {
                playback_reset(m);
                *out = MAKTABASAUTI_UNKNOWN_LENGTH;
                return 1;
            }
            if (!channel_audible(m)) break;
        }
        end_tick(m);
        if (m->err) return 0;
    }
    if (m->err) return 0;
    *out = n;
    playback_reset(m);
    return 1;
}

/* Jedno slowo DMA na wygasniecie okresu modulatora.  Petla w slowach.  Jednorazowa zostaje na ostatnim. */
static uint32_t mod_words(const voice_t *v) {
    return v->length / 2u;
}

static void mod_wrap(voice_t *v) {
    uint32_t n = mod_words(v);
    uint32_t start, len;
    if (!n) return;
    if (v->loop_len >= 2u) {
        start = v->loop_start / 2u;
        len = v->loop_len / 2u;
        if (!len || start >= n) {
            if (v->mod_idx >= n) v->mod_idx = n - 1u;
            return;
        }
        if (v->mod_idx >= start + len) v->mod_idx = start + (v->mod_idx - start) % len;
        return;
    }
    if (v->mod_idx >= n) v->mod_idx = n - 1u;
}

static uint16_t mod_word(const voice_t *v) {
    uint32_t n = mod_words(v);
    uint32_t i = v->mod_idx;
    const uint8_t *p;
    if (!v->sample || !n) return 0;
    if (i >= n) i = n - 1u;
    p = v->sample + (size_t)i * 2u;
    return (uint16_t)(((unsigned)p[0] << 8) | p[1]);
}

static void mod_clock(voice_t *v, uint32_t rate, uint32_t clock) {
    uint64_t den;
    uint32_t n, add, per;
    per = v->play_period ? v->play_period : v->period;
    if (!per || !rate || !clock) return;
    den = (uint64_t)per * rate;
    v->mod_frac += clock;
    add = (uint32_t)(v->mod_frac / den);
    v->mod_frac %= den;
    if (!add) return;
    n = mod_words(v);
    if (!n) return;
    if (v->loop_len >= 2u) {
        v->mod_idx += add;
        mod_wrap(v);
        return;
    }
    if (v->mod_idx >= n - 1u) return;
    if (add >= n - v->mod_idx) v->mod_idx = n - 1u;
    else v->mod_idx += add;
}

/* Bity USE*P* / USE*V* Amigi: kanal pisze do nastepnego.  Kanal 3 tylko milknie.
 * Oba naraz: parzyste slowo to glosnosc, nieparzyste okres.  Biezace slowo gra od razu. */
static void mod_drive(mod_t *m, unsigned ch, voice_t *v) {
    voice_t *dst;
    uint16_t w;
    int vol_turn, per_turn;
    unsigned vol;
    if (ch + 1u >= m->channels || !v->sample) return;
    dst = &m->voice[ch + 1u];
    w = mod_word(v);
    if (v->mod_vol && v->mod_per) {
        vol_turn = (v->mod_idx & 1u) == 0u;
        per_turn = !vol_turn;
    } else {
        vol_turn = v->mod_vol;
        per_turn = v->mod_per;
    }
    if (vol_turn) {
        vol = w & 0x7Fu;
        if (vol > 64u) vol = 64u;
        dst->volume = (uint8_t)vol;
    }
    if (per_turn) {
        if (!w) w = 1u;
        dst->period = w;
        use_period(dst, w);
    }
    mod_clock(v, m->rate, m->clock);
}

static void mix_one(mod_t *m, int16_t *dst) {
    int32_t acc[2];
    int32_t left, right;
    unsigned ch;
    acc[0] = 0;
    acc[1] = 0;
    for (ch = 0; ch < m->channels; ch++) {
        voice_t *v = &m->voice[ch];
        int vol;
        if (v->mod_silent) {
            mod_drive(m, ch, v);
            continue;
        }
        vol = (int)v->volume + (int)v->tremolo;
        if (vol < 0) vol = 0;
        if (vol > 64) vol = 64;
        settle(v);
        if (v->play_period && v->sample && v->idx < v->length) {
            const uint8_t *flip = NULL;
            if (v->inst >= 1u && v->inst <= m->instruments) flip = m->inst[v->inst - 1u].flip;
            maktabasauti_mod_voice_add(acc, v->sample, flip, v->idx, v->length, m->clear_n, vol, v->pan);
        }
        voice_advance(v, m->rate, m->clock);
    }
    left = acc[0];
    right = acc[1];
    if (m->amiga_rc) {
        double l = (double)left, r = (double)right;
        m->rca_y += m->rc_a * (l - m->rca_y);
        m->rcb_y += m->rc_a * (r - m->rcb_y);
        left = (int32_t)(m->rca_y >= 0 ? m->rca_y + 0.5 : m->rca_y - 0.5);
        right = (int32_t)(m->rcb_y >= 0 ? m->rcb_y + 0.5 : m->rcb_y - 0.5);
    }
    if (m->filter_on) {
        double l = (double)left, r = (double)right;
        double ly = m->b0 * l + m->b1 * m->lx1 + m->b2 * m->lx2 - m->a1 * m->ly1 - m->a2 * m->ly2;
        double ry = m->b0 * r + m->b1 * m->rx1 + m->b2 * m->rx2 - m->a1 * m->ry1 - m->a2 * m->ry2;
        m->lx2 = m->lx1; m->lx1 = l; m->ly2 = m->ly1; m->ly1 = ly;
        m->rx2 = m->rx1; m->rx1 = r; m->ry2 = m->ry1; m->ry1 = ry;
        left = (int32_t)(ly >= 0 ? ly + 0.5 : ly - 0.5);
        right = (int32_t)(ry >= 0 ? ry + 0.5 : ry - 0.5);
    }
    dst[0] = sat16(left);
    dst[1] = sat16(right);
}

static maktabasauti_result_t render(mod_t *m, int16_t *dst, size_t dst_frames, size_t *frames) {
    size_t wrote = 0;
    if (!frames) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (dst_frames && !dst) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (!dst_frames) { *frames = 0; return MAKTABASAUTI_OK; }
    while (wrote < dst_frames) {
        uint32_t chunk, i;
        if (!m->have_tick || m->tick_left == 0u) {
            if (m->have_tick) end_tick(m);
            if (!begin_tick(m)) break;
            if (m->tick_left == 0u) continue;
        }
        chunk = m->tick_left;
        if ((size_t)chunk > dst_frames - wrote) chunk = (uint32_t)(dst_frames - wrote);
        for (i = 0; i < chunk; i++) mix_one(m, dst + (wrote + i) * 2u);
        m->tick_left -= chunk;
        m->frame_pos += chunk;
        wrote += chunk;
    }
    *frames = wrote;
    if (!wrote) return MAKTABASAUTI_EOF;
    m->err = NULL;
    return MAKTABASAUTI_OK;
}

static int parse_rate(const maktabasauti_host_t *h, uint32_t *rate) {
    const char *s = NULL;
    uint32_t v = 0;
    if (h->option) {
        s = h->option(h->user, "sample_rate");
        if (!s) s = h->option(h->user, "mod.sample_rate");
    }
    if (!s || !s[0]) { *rate = 44100u; return 1; }
    for (; *s; s++) {
        uint32_t d;
        if (*s < '0' || *s > '9') return 0;
        d = (uint32_t)(*s - '0');
        if (v > (4294967295u - d) / 10u) return 0;
        v = v * 10u + d;
    }
    if (v == 0u || v > 384000u) return 0;
    *rate = v;
    return 1;
}

static int same_word(const char *s, const char *w) {
    for (; *w; s++, w++) {
        unsigned c = (unsigned char)*s;
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        if (c != (unsigned char)*w) return 0;
    }
    return *s == 0;
}

/* Plik nie niesie zegara.  Brak opcji i "pal" zostaja przy PAL.  "ntsc" jest jedynym drugim zegarem. */
static int parse_clock(const maktabasauti_host_t *h, uint32_t *clock) {
    const char *s = NULL;
    if (h->option) s = h->option(h->user, "mod.clock");
    if (!s || !s[0] || same_word(s, "pal")) { *clock = PAULA_PAL; return 1; }
    if (same_word(s, "ntsc")) { *clock = PAULA_NTSC; return 1; }
    return 0;
}

/* Brak opcji: bez stalego RC.  a500 wlacza jednobiegunowy dolnoprzepust okolo 4,9 kHz. */
static int parse_model(const maktabasauti_host_t *h, mod_t *m) {
    const char *s = NULL;
    if (h->option) s = h->option(h->user, "mod.model");
    if (!s || !s[0] || same_word(s, "a1200") || same_word(s, "pc")) {
        m->amiga_rc = 0;
        return 1;
    }
    if (same_word(s, "a500")) {
        m->amiga_rc = 1;
        return 1;
    }
    return 0;
}

/* Plik nie niesie wersji trackera.  Brak opcji i pt/protracker/pt11 zostaja przy ProTrackerze 1.1.
 * pt10 wlacza czterobajtowe czyszczenie i Funk Repeat.  ust wlacza grupe 1 Ultimate Soundtrackera.
 * st2 to II-VI z pozniejszego zestawienia.  tjc to Jungle Command.  st23/st24 to DOC 2.3/2.4.
 * st25 to Soundtracker 2.5.  st9 to IX i Master. */
static int classify_tracker(const maktabasauti_host_t *h, uint8_t *kind, uint8_t *clear_n) {
    const char *s = NULL;
    *kind = TRACK_PT11;
    *clear_n = 2;
    if (h->option) s = h->option(h->user, "mod.tracker");
    if (!s || !s[0] || same_word(s, "pt") || same_word(s, "protracker") || same_word(s, "pt11")) return 1;
    if (same_word(s, "pt10") || same_word(s, "protracker10")) {
        *kind = TRACK_PT10;
        *clear_n = 4;
        return 1;
    }
    if (same_word(s, "ust") || same_word(s, "ultimate")) {
        *kind = TRACK_UST;
        *clear_n = 0;
        return 1;
    }
    if (same_word(s, "st2") || same_word(s, "soundtracker2")) {
        *kind = TRACK_ST2;
        *clear_n = 0;
        return 1;
    }
    if (same_word(s, "st9") || same_word(s, "master")) {
        *kind = TRACK_ST9;
        *clear_n = 0;
        return 1;
    }
    if (same_word(s, "tjc") || same_word(s, "jungle")) {
        *kind = TRACK_TJC;
        *clear_n = 0;
        return 1;
    }
    if (same_word(s, "st23") || same_word(s, "st24")) {
        *kind = TRACK_ST23;
        *clear_n = 0;
        return 1;
    }
    if (same_word(s, "st25")) {
        *kind = TRACK_ST25;
        *clear_n = 0;
        return 1;
    }
    return 0;
}

static double reduce_rad(double x) {
    const double pi = 3.14159265358979323846;
    const double twopi = 6.28318530717958647692;
    double k = x / twopi;
    int n = (int)(k >= 0.0 ? k + 0.5 : k - 0.5);
    x -= twopi * (double)n;
    if (x > pi) x -= twopi;
    if (x < -pi) x += twopi;
    return x;
}

static double tsin(double x) {
    double x2, term, s;
    int i;
    x = reduce_rad(x);
    x2 = x * x;
    term = x;
    s = x;
    for (i = 1; i <= 9; i++) {
        term *= -x2 / (double)((2 * i) * (2 * i + 1));
        s += term;
    }
    return s;
}

static double tcos(double x) {
    double x2, term, s;
    int i;
    x = reduce_rad(x);
    x2 = x * x;
    term = 1.0;
    s = 1.0;
    for (i = 1; i <= 9; i++) {
        term *= -x2 / (double)((2 * i - 1) * (2 * i));
        s += term;
    }
    return s;
}

static double texp(double x) {
    double term = 1.0, s = 1.0;
    int i;
    for (i = 1; i <= 20; i++) {
        term *= x / (double)i;
        s += term;
    }
    return s;
}

/* E0x: Butterworth 2. rzedu, 3275 Hz, wzmocnienie DC = 1.  Bez libm.  RC A500 jest osobna opcja. */
static void design_filters(mod_t *m) {
    const double pi = 3.14159265358979323846;
    const double sqrt2 = 1.4142135623730950488;
    double fs = (double)m->rate;
    double fc = 3275.0;
    double w0, alpha, cosw, b0, b1, a0;
    if (fc * 2.0 >= fs) fc = fs * 0.45;
    w0 = 2.0 * pi * fc / fs;
    cosw = tcos(w0);
    alpha = tsin(w0) / sqrt2;
    b0 = (1.0 - cosw) * 0.5;
    b1 = 1.0 - cosw;
    a0 = 1.0 + alpha;
    m->b0 = b0 / a0;
    m->b1 = b1 / a0;
    m->b2 = b0 / a0;
    m->a1 = -2.0 * cosw / a0;
    m->a2 = (1.0 - alpha) / a0;
    m->rc_a = 0;
    if (m->amiga_rc) {
        double cut = 4900.0;
        double k;
        if (cut * 2.0 >= fs) cut = fs * 0.45;
        k = -2.0 * pi * cut / fs;
        m->rc_a = 1.0 - texp(k);
        if (m->rc_a < 0.0) m->rc_a = 0.0;
        if (m->rc_a > 1.0) m->rc_a = 1.0;
    }
}

static void copy_name(char *dst, size_t cap, const uint8_t *src, size_t n) {
    size_t i;
    if (n >= cap) n = cap - 1u;
    for (i = 0; i < n; i++) dst[i] = (char)src[i];
    dst[n] = 0;
}

static void release_flips(mod_t *m) {
    unsigned i;
    const maktabasauti_host_t *h = m->host;
    if (!h || !h->free) return;
    for (i = 0; i < MOD_INST; i++) {
        if (!m->inst[i].flip) continue;
        h->free(h->user, m->inst[i].flip);
        m->inst[i].flip = NULL;
    }
}

static int alloc_flips(mod_t *m) {
    unsigned i;
    for (i = 0; i < m->instruments; i++) {
        inst_t *in = &m->inst[i];
        size_t bytes;
        if (!in->length) continue;
        bytes = ((size_t)in->length + 7u) / 8u;
        in->flip = (uint8_t *)m->host->alloc(m->host->user, bytes);
        if (!in->flip) {
            release_flips(m);
            m->err = "brak pamieci na maske invert loop";
            return 0;
        }
        memset(in->flip, 0, bytes);
    }
    return 1;
}

static maktabasauti_result_t parse_module(mod_t *m, const uint8_t *p, size_t n, const ident_t *id) {
    unsigned max_order = 0, span;
    size_t info_at, off, cell;
    unsigned i;
    if (n < id->header) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    m->channels = id->channels;
    m->instruments = id->instruments;
    m->flt8 = id->flt8;
    memcpy(m->magic, id->magic, 5u);
    info_at = 20u + (size_t)id->instruments * 30u;
    m->song_length = p[info_at];
    m->restart = p[info_at + 1u];
    if (m->song_length < 1u || m->song_length > 128u) {
        m->err = "dlugosc utworu poza 1..128";
        return MAKTABASAUTI_E_CORRUPT_DATA;
    }
    memcpy(m->orders, p + info_at + 2u, 128u);
    for (i = 0; i < 128u; i++) if (m->orders[i] > max_order) max_order = m->orders[i];
    /* FLT8: pozycja P potrzebuje patternow P i P+1, wiec zapisanych jest max+2. */
    m->pattern_count = (uint16_t)(max_order + 1u + (id->flt8 ? 1u : 0u));
    if ((unsigned)m->pattern_count > id->pattern_limit) {
        m->err = "za duzo patternow dla tego magika";
        return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    }
    span = id->flt8 ? 4u : id->channels;
    off = (size_t)id->header + (size_t)m->pattern_count * 64u * span * 4u;
    if (off > n) {
        m->err = "pattern urywa sie przed koncem pliku";
        return MAKTABASAUTI_E_TRUNCATED;
    }
    m->patterns = p + id->header;
    copy_name(m->title, sizeof m->title, p, 20u);
    for (i = 0; i < id->instruments; i++) {
        const uint8_t *ip = p + 20u + (size_t)i * 30u;
        uint16_t words = be16(ip + 22);
        uint16_t loop_w = be16(ip + 26);
        uint16_t loop_l = be16(ip + 28);
        uint32_t bytes = (uint32_t)words * 2u;
        uint8_t nib = (uint8_t)(ip[24] & 0x0Fu);
        inst_t *in = &m->inst[i];
        copy_name(in->name, sizeof in->name, ip, 22u);
        in->finetune = nib < 8u ? (int8_t)nib : (int8_t)((int)nib - 16);
        in->volume = ip[25] > 64u ? 64u : ip[25];
        in->length = bytes;
        in->data = NULL;
        in->loop_start = 0;
        in->loop_len = 0;
        if (bytes) {
            if (off > n || bytes > n - off) {
                m->err = "probka urywa sie przed koncem pliku";
                return MAKTABASAUTI_E_TRUNCATED;
            }
            in->data = p + off;
            off += bytes;
        }
        /* SoundTracker trzyma poczatek petli w bajtach i gra tylko petle.
         * Grupa 1 i II-VI koncza sie na 9998 bajtach.  IX i Master siegaja 32 KB.
         * Prefiks ProTrackera tu nie dziala. */
        if (st_family(m->tracker)) {
            uint32_t cap = (m->tracker == TRACK_ST9 || m->tracker == TRACK_ST23) ? 32768u : 9998u;
            if (bytes > cap) {
                m->err = m->tracker == TRACK_UST
                    ? "probka Ultimate Soundtrackera dluzsza niz 9998 bajtow"
                    : "probka SoundTrackera przekracza limit tej grupy";
                return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
            }
            if (loop_l > 1u) {
                uint32_t ls = loop_w;
                uint32_t ll = (uint32_t)loop_l * 2u;
                if (ls > bytes || ll > bytes - ls) {
                    m->err = "petla probki wychodzi poza jej koniec";
                    return MAKTABASAUTI_E_CORRUPT_DATA;
                }
                if (in->data) in->data += ls;
                in->length = ll;
                in->loop_start = 0;
                in->loop_len = ll;
            }
        } else if (loop_l > 1u) {
            uint32_t ls = (uint32_t)loop_w * 2u;
            uint32_t ll = (uint32_t)loop_l * 2u;
            if (ls > bytes || ll > bytes - ls) {
                m->err = "petla probki wychodzi poza jej koniec";
                return MAKTABASAUTI_E_CORRUPT_DATA;
            }
            in->loop_start = ls;
            in->loop_len = ll;
        }
        if (m->tracker == TRACK_ST25 && bytes > 65536u) {
            m->err = "probka SoundTrackera przekracza limit tej grupy";
            return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
        }
    }
    /* Bajty 0x1D7 to tempo, nie pozycja restartu.  120 daje BPM 121.  Od 240 mianownik by zniknal.
     * Jungle Command i DOC 2.3/2.5 tego wzoru nie uzywaja.  Graja 125 BPM, a bajt nie jest pozycja. */
    if ((m->tracker == TRACK_UST || m->tracker == TRACK_ST2 || m->tracker == TRACK_ST9) && m->instruments == 15u) {
        unsigned t = m->restart;
        double bpm;
        if (t >= 240u) t = 239u;
        bpm = (709378.92 * 125.0) / (50.0 * (double)(240u - t) * 122.0);
        m->ust_tempo = (uint32_t)(bpm + 0.5);
        if (!m->ust_tempo) m->ust_tempo = 1u;
        m->restart = 0;
    }
    if (m->tracker == TRACK_TJC || m->tracker == TRACK_ST23 || m->tracker == TRACK_ST25) m->restart = 0;
    for (cell = 0; cell < (size_t)m->pattern_count * 64u * span; cell++) {
        const uint8_t *c = m->patterns + cell * 4u;
        unsigned sample = (c[0] & 0xF0u) | (c[2] >> 4);
        if (sample > id->instruments) {
            m->err = "numer probki spoza zakresu instrumentow";
            return MAKTABASAUTI_E_CORRUPT_DATA;
        }
    }
    /* Zapas na EEx (x16) i jeden przebieg E6x.  Modul 15-instrumentowy liczy Fxx jako predkosc do 255. */
    m->tick_cap = (uint32_t)m->song_length * 64u * (m->instruments == 15u ? 255u : 31u) * 20u;
    m->file = p;
    if (!alloc_flips(m)) return MAKTABASAUTI_E_NO_MEMORY;
    return MAKTABASAUTI_OK;
}

/* Rozszerzenie za ostatnia kropka nazwy, po ostatnim ukosniku.  Wielkosc liter nie gra roli. */
static int hint_wow(const char *hint) {
    const char *base, *dot, *s;
    char a, b, c;
    if (!hint || !hint[0]) return 0;
    base = hint;
    for (s = hint; *s; s++) if (*s == '/' || *s == '\\') base = s + 1;
    dot = NULL;
    for (s = base; *s; s++) if (*s == '.') dot = s;
    if (!dot || dot == base || !dot[1] || !dot[2] || !dot[3] || dot[4]) return 0;
    a = dot[1]; b = dot[2]; c = dot[3];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return a == 'w' && b == 'o' && c == 'w';
}

static int32_t MAKTABASAUTI_CALL mod_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    ident_t id;
    /* Mod's Grave: samo rozszerzenie .wow i magik M.K.  Rozmiar, restart i glosnosc nie decyduja.
     * Bez tej nazwy zwykly M.K. zostaje czterokanalowy, takze gdy ogon ma wielkosc osmiu kanalow. */
    if (self && self->user_data) {
        if (!p || n < MOD_HEADER_31 || !hint_wow(hint)) return 0;
        if (memcmp(p + 1080, "M.K.", 4) != 0) return 0;
        return 90;
    }
    (void)hint;
    if (!p || !recognize(p, n, &id)) return 0;
    return id.header == MOD_HEADER_15 ? 60 : 70;
}

static maktabasauti_result_t MAKTABASAUTI_CALL mod_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h,
                                                       maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    const uint8_t *data = NULL;
    size_t n = 0;
    mod_t *m;
    ident_t id;
    maktabasauti_result_t r;
    int wow = self && self->user_data;
    if (!h || !info || !state || !h->alloc || !h->free || !h->map) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    *state = NULL;
    r = h->map(h->user, rd, &data, &n);
    if (r != MAKTABASAUTI_OK) return r;
    if (!data || !recognize(data, n, &id)) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    /* Deskryptor Grave wymusza osiem plaskich kanalow.  Za krotki plik urywa sie przy patternie. */
    if (wow) {
        if (id.instruments != 31u || memcmp(id.magic, "M.K.", 4) != 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        id.channels = 8;
        id.pattern_limit = 256;
        memcpy(id.magic, "WOW", 4u);
        id.magic[4] = 0;
    }
    m = (mod_t *)h->alloc(h->user, sizeof *m);
    if (!m) return MAKTABASAUTI_E_NO_MEMORY;
    memset(m, 0, sizeof *m);
    m->host = h;
    {
        uint8_t kind = TRACK_PT11, clear_n = 2;
        int tracker_ok = classify_tracker(h, &kind, &clear_n);
        m->tracker = kind;
        m->clear_n = clear_n;
        r = parse_module(m, data, n, &id);
        if (r != MAKTABASAUTI_OK) {
            release_flips(m);
            h->free(h->user, m);
            return r;
        }
        if (!tracker_ok) {
            release_flips(m);
            h->free(h->user, m);
            return MAKTABASAUTI_E_INVALID_ARGUMENT;
        }
    }
    if (!parse_rate(h, &m->rate)) {
        release_flips(m);
        h->free(h->user, m);
        return MAKTABASAUTI_E_INVALID_ARGUMENT;
    }
    if (!parse_clock(h, &m->clock)) {
        release_flips(m);
        h->free(h->user, m);
        return MAKTABASAUTI_E_INVALID_ARGUMENT;
    }
    if (!parse_model(h, m)) {
        release_flips(m);
        h->free(h->user, m);
        return MAKTABASAUTI_E_INVALID_ARGUMENT;
    }
    design_filters(m);
    if (!count_frames(m, &m->total_frames)) {
        r = m->err ? MAKTABASAUTI_E_CORRUPT_DATA : MAKTABASAUTI_E_INTERNAL;
        release_flips(m);
        h->free(h->user, m);
        return r;
    }
    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info;
    info->sample_rate = m->rate;
    info->channels = 2u;
    info->flags = MAKTABASAUTI_STREAM_SEEKABLE;
    info->channel_mask = MAKTABASAUTI_LAYOUT_STEREO;
    info->native_format = MAKTABASAUTI_FORMAT_S16;
    info->bits_per_sample = 16u;
    info->total_frames = m->total_frames;
    memcpy(info->codec_name, "mod", 4u);
    memcpy(info->codec_profile, m->magic, 4u);
    memcpy(info->container_name, "mod", 4u);
    *state = m;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL mod_read(void *state, void *dst, size_t dst_frames, size_t *frames) {
    if (!state || !frames) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    return render((mod_t *)state, (int16_t *)dst, dst_frames, frames);
}

static maktabasauti_result_t MAKTABASAUTI_CALL mod_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    mod_t *m = (mod_t *)state;
    int16_t junk[128 * 2];
    (void)flags;
    if (!m || !actual) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (m->total_frames != MAKTABASAUTI_UNKNOWN_LENGTH) {
        if (frame > m->total_frames) {
            m->err = "pozycja poza koncem utworu";
            return MAKTABASAUTI_E_OUT_OF_RANGE;
        }
    } else {
        uint64_t room = (uint64_t)m->tick_cap * ((uint64_t)m->rate * 5u / 2u + 1u);
        if (frame > m->frame_pos && frame - m->frame_pos > room) {
            m->err = "pozycja poza koncem utworu";
            return MAKTABASAUTI_E_OUT_OF_RANGE;
        }
    }
    if (frame < m->frame_pos) playback_reset(m);
    while (m->frame_pos < frame) {
        size_t want = (size_t)(frame - m->frame_pos);
        size_t got = 0;
        maktabasauti_result_t r;
        if (want > 128u) want = 128u;
        r = render(m, junk, want, &got);
        if (r < 0) return r;
        if (!got) {
            m->err = "seek zatrzymal sie przed celem";
            return MAKTABASAUTI_E_INTERNAL;
        }
    }
    *actual = m->frame_pos;
    m->err = NULL;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL mod_reset(void *state) {
    if (!state) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    playback_reset((mod_t *)state);
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL mod_flush(void *state) {
    if (!state) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    ((mod_t *)state)->err = NULL;
    return MAKTABASAUTI_OK;
}

static void put_i32(char *dst, int v) {
    char rev[8];
    unsigned u;
    int n = 0, i = 0;
    if (v < 0) { dst[i++] = '-'; v = -v; }
    u = (unsigned)v;
    do { rev[n++] = (char)('0' + (u % 10u)); u /= 10u; } while (u);
    while (n) dst[i++] = rev[--n];
    dst[i] = 0;
}

static maktabasauti_result_t write_tag(char *buf, size_t cap, size_t *needed, const char *s) {
    size_t n = strlen(s) + 1u;
    if (needed) *needed = n;
    if (!buf || cap < n) return MAKTABASAUTI_E_BUFFER_TOO_SMALL;
    memcpy(buf, s, n);
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL mod_get_tag(const void *state, const char *key, char *buf, size_t cap, size_t *needed) {
    const mod_t *m = (const mod_t *)state;
    char num[8];
    unsigned idx;
    const char *p;
    if (!m || !key) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (strcmp(key, "title") == 0) return write_tag(buf, cap, needed, m->title);
    if (strcmp(key, "magic") == 0) return write_tag(buf, cap, needed, m->magic);
    if (strcmp(key, "restart") == 0) {
        put_i32(num, (int)m->restart);
        return write_tag(buf, cap, needed, num);
    }
    if (strncmp(key, "finetune.", 9) == 0) p = key + 9;
    else if (strncmp(key, "name.", 5) == 0) p = key + 5;
    else return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    idx = 0;
    if (!*p) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    for (; *p; p++) {
        if (*p < '0' || *p > '9') return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
        idx = idx * 10u + (unsigned)(*p - '0');
        if (idx > MOD_INST) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    }
    if (idx < 1u || idx > m->instruments) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    if (strncmp(key, "name.", 5) == 0) return write_tag(buf, cap, needed, m->inst[idx - 1u].name);
    put_i32(num, m->inst[idx - 1u].finetune);
    return write_tag(buf, cap, needed, num);
}

static const char *MAKTABASAUTI_CALL mod_last_error(const void *state) {
    return state ? ((const mod_t *)state)->err : NULL;
}

static void MAKTABASAUTI_CALL mod_close(void *state) {
    mod_t *m = (mod_t *)state;
    const maktabasauti_host_t *h;
    if (!m) return;
    h = m->host;
    release_flips(m);
    h->free(h->user, m);
}

const maktabasauti_codec_t maktabasauti_mod_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "mod",
    "ProTracker / SoundTracker module (15- and 31-instrument)", "maktabasauti_mod",
    MAKTABASAUTI_CODEC_FLAG_NEEDS_FULL_BUFFER | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK | MAKTABASAUTI_CODEC_FLAG_HAS_TAGS | MAKTABASAUTI_CODEC_FLAG_EXPERIMENTAL,
    0u, NULL,
    mod_probe, mod_open, mod_read, mod_seek, mod_reset, mod_flush, mod_get_tag, mod_last_error, NULL, mod_close,
    { NULL }, { NULL }
};

static const char k_wow_tag[] = "wow";

const maktabasauti_codec_t maktabasauti_mod_wow_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "wow",
    "Mod's Grave module (8-channel M.K.)", "maktabasauti_mod",
    MAKTABASAUTI_CODEC_FLAG_NEEDS_FULL_BUFFER | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK | MAKTABASAUTI_CODEC_FLAG_HAS_TAGS | MAKTABASAUTI_CODEC_FLAG_EXPERIMENTAL,
    0u, k_wow_tag,
    mod_probe, mod_open, mod_read, mod_seek, mod_reset, mod_flush, mod_get_tag, mod_last_error, NULL, mod_close,
    { NULL }, { NULL }
};
