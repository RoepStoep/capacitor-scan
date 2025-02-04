
// includes

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "bit.hpp"
#include "common.hpp"
#include "eval.hpp"
#include "filestream.hpp"
#include "libmy.hpp"
#include "pos.hpp"
#include "score.hpp"
#include "thread.hpp"
#include "var.hpp"

// compile-time functions

constexpr int pow(int a, int b) { return (b == 0) ? 1 : pow(a, b - 1) * a; }

// constants

const int Pattern_Size {12}; // squares per pattern
const int P {2125820}; // eval parameters
const int Unit {10}; // units per cp

// "constants"

const int Perm_0[Pattern_Size] { 11, 10,  7,  6,  3,  2,  9,  8,  5,  4,  1,  0 };
const int Perm_1[Pattern_Size] {  0,  1,  4,  5,  8,  9,  2,  3,  6,  7, 10, 11 };

static int PST_I[Dense_Size]{ // for white, normal variant
      +00,  +00,  +00,  +00,  +00,
   +18,  +87,  +78,  +74,  102,
      +43,  +21,  +22,  +30,  +04,
   -04,   +8,  -03,  +00,  +00,
      -13,  -13,  -07,  -13,   -8,
   -11,  -16,   -9,  -13,  -21,
      -20,  -15,  -10,  -18,  -17,
   -20,  -18,  -13,  -14,  -17,
      -23,  -16,  -14,  -17,  -19,
   -23,  -15,  -12,  -15,  -17,
};

static int PST_B[Dense_Size]{ // for white, breakthrough variant
      +00,  +00,  +00,  +00,  +00,
   +05,  +54,  +90,  +82,  163,
      128,  +57,  +73,  +84,  +07,
   -18,  +44,  +14,  +20,  +13,
      -22,  -12,  -04,  -06,  -15,
   -25,  -15,  -05,  -14,  -29,
      -33,  -17,  -10,  -28,  -34,
   -35,  -32,  -21,  -20,  -33,
      -43,  -28,  -26,  -33,  -42,
   -52,  -33,  -29,  -30,  -41,
};

static int PST_L[Dense_Size]{ // for white, losing variant
      +00,  +00,  +00,  +00,  +00,
   -71,  +24,  +38,  +16,  +40,  
      +05,  +30,  +20,  -06,  -70,
   -15,  -06,  +13,   +8,  -05,  
      +01,  +06,  +05,  -07,  -23,
   -11,  -05,  +10,  +02,  -05,  
       -8,  +07,   +8,  -01,  -17,
   -23,  +04,  +17,   +8,   -7,  
      -11,  +17,  +24,  +07,  -22,
   -38,  +04,  +25,  +17,  -03,  
};

static int PST_F[Dense_Size]{ // for white, frisian variant
      +00,  +00,  +00,  +00,  +00,
   +81,  144,  105,  102,  171,
      +93,  +42,  +22,  +61,  +33,
   -23,  +10,  -25,  -30,  +07,
      -16,  -45,  -49,  -19,  -14,
   -23,  -40,  -60,  -52,  -32,
      -28,  -47,  -48,  -36,  -20,
   -24,  -29,  -42,  -35,  -25,
      -17,  -17,  -25,  -12,  +00,
   +04,  -01,  -01,  -01,  +11,
};

// variables

static std::vector<int> G_Weight;

static int Trits_0[pow(2, Pattern_Size)];
static int Trits_1[pow(2, Pattern_Size)];

// types

class Score_2 {

private:

   int m_mg {0};
   int m_eg {0};

public:

   void add(int var, int val) {
      m_mg += G_Weight[var * 2 + 0] * val;
      m_eg += G_Weight[var * 2 + 1] * val;
   }

   int mg () const { return m_mg; }
   int eg () const { return m_eg; }
};

// prototypes

static int conv (int index, int size, int bf, int bt, const int perm[]);

static void pst      (Score_2 & s2, int var, Bit bw, Bit bb);
static void king_mob (Score_2 & s2, int var, const Pos & pos);
static void pattern  (Score_2 & s2, int var, const Pos & pos);

static void indices_column (uint64 white, uint64 black, int & index_top, int & index_bottom);
static void indices_column (uint64 b, int & i0, int & i2);

static Bit attacks (const Pos & pos, Side sd);

static Score eval_pst(const Pos& pos);
static int   material(const Pos& pos, Side sd);
static int   pst(const Pos& pos, Side sd);

// functions

void eval_init() {

   sync_cout << "init eval" << sync_endl;

   // load weights

   std::string file_name = std::string("eval") + var::variant_name();
   std::unique_ptr<std::istream> file = get_stream_binary(file_name);

   if (!file) {
      sync_cout << "error: unable to open file \"" << file_name << "\"" << sync_endl;
      std::exit(EXIT_FAILURE);
   }

   G_Weight.resize(P * 2);

   for (int i = 0; i < P * 2; i++) {
      G_Weight[i] = int16(ml::get_bytes(*file, 2)); // HACK: extend sign
   }

   // init base conversion (2 -> 3)

   int size = Pattern_Size;
   int bf = 2;
   int bt = 3;

   for (int i = 0; i < pow(bf, size); i++) {
      Trits_0[i] = conv(i, size, bf, bt, Perm_0);
      Trits_1[i] = conv(i, size, bf, bt, Perm_1);
   }
}

static int conv(int index, int size, int bf, int bt, const int perm[]) {

   assert(index >= 0 && index < pow(bf, size));

   int from = index;
   int to = 0;

   for (int i = 0; i < size; i++) {

      int digit = from % bf;
      from /= bf;

      int j = perm[i];
      assert(j >= 0 && j < size);

      assert(digit >= 0 && digit < bt);
      to += digit * pow(bt, j);
   }

   assert(from == 0);

   assert(to >= 0 && to < pow(bt, size));
   return to;
}

Score eval(const Pos & pos) {

   if (var::Eval == var::PST) return eval_pst(pos);

   // features

   Score_2 s2;
   int var = 0;

   // material

   int nwm = bit::count(pos.wm());
   int nbm = bit::count(pos.bm());
   int nwk = bit::count(pos.wk());
   int nbk = bit::count(pos.bk());

   s2.add(var + 0, nwm - nbm);
   s2.add(var + 1, (nwk >= 1) - (nbk >= 1));
   s2.add(var + 2, std::max(nwk - 1, 0) - std::max(nbk - 1, 0));
   var += 3;

   // king position

   pst(s2, var, pos.wk(), pos.bk());
   var += Dense_Size;

   // king mobility

   king_mob(s2, var, pos);
   var += 2;

   // left/right balance

   if (var::Variant != var::Losing) {
      s2.add(var, std::abs(pos::skew(pos, White)) - std::abs(pos::skew(pos, Black)));
   }
   var += 1;

   // patterns

   pattern(s2, var, pos);
   var += pow(3, Pattern_Size) * 4;

   // game phase

   int stage = pos::stage(pos);
   assert(stage >= 0 && stage <= Stage_Size);

   int sc = ml::div_round(s2.mg() * (Stage_Size - stage) + s2.eg() * stage, Unit * Stage_Size);

   // drawish material

   if (var::Variant == var::Normal) {

      if (sc > 0 && nbk != 0) { // white ahead

         if (nwm + nwk <= 3) {
            sc /= 8;
         } else if (nwk == nbk && std::abs(nwm - nbm) <= 1) {
            sc /= 2;
         }

      } else if (sc < 0 && nwk != 0) { // black ahead

         if (nbm + nbk <= 3) {
            sc /= 8;
         } else if (nwk == nbk && std::abs(nwm - nbm) <= 1) {
            sc /= 2;
         }
      }
   }

   return score::clamp(score::side(Score(sc), pos.turn())); // for side to move
}

static void pst(Score_2 & s2, int var, Bit bw, Bit bb) {

   for (Square sq : bw) {
      s2.add(var + square_dense(sq), +1);
   }

   for (Square sq : bb) {
      s2.add(var + square_dense(square_opp(sq)), -1);
   }
}

static void king_mob(Score_2 & s2, int var, const Pos & pos) {

   int ns = 0;
   int nd = 0;

   Bit be = pos.empty();

   // white

   if (pos.wk() != 0) {

      Bit attacked = attacks(pos, Black);

      for (Square from : pos.wk()) {

         Bit atk  = bit::king_moves(from, be) & be;
         Bit safe = atk & ~attacked;
         Bit deny = atk &  attacked;

         ns += bit::count(safe);
         nd += bit::count(deny);
      }
   }

   // black

   if (pos.bk() != 0) {

      Bit attacked = attacks(pos, White);

      for (Square from : pos.bk()) {

         Bit atk  = bit::king_moves(from, be) & be;
         Bit safe = atk & ~attacked;
         Bit deny = atk &  attacked;

         ns -= bit::count(safe);
         nd -= bit::count(deny);
      }
   }

   s2.add(var + 0, ns);
   s2.add(var + 1, nd);
}

static void pattern(Score_2 & s2, int var, const Pos & pos) {

   int i0, i1, i2, i3; // top
   int i4, i5, i6, i7; // bottom

   indices_column(pos.wm() >> 0, pos.bm() >> 0, i0, i4);
   indices_column(pos.wm() >> 1, pos.bm() >> 1, i1, i5);
   indices_column(pos.wm() >> 2, pos.bm() >> 2, i2, i6);
   indices_column(pos.wm() >> 3, pos.bm() >> 3, i3, i7);

   s2.add(var +  265720 + i0, +1);
   s2.add(var +  797161 + i1, +1);
   s2.add(var + 1328602 + i2, +1);
   s2.add(var + 1860043 + i3, +1);

   s2.add(var + 1860043 - i4, -1);
   s2.add(var + 1328602 - i5, -1);
   s2.add(var +  797161 - i6, -1);
   s2.add(var +  265720 - i7, -1);
}

static void indices_column(uint64 white, uint64 black, int & index_top, int & index_bottom) {

   int w0, w2;
   int b0, b2;

   indices_column(white, w0, w2);
   indices_column(black, b0, b2);

   index_top    = Trits_0[b0] - Trits_0[w0];
   index_bottom = Trits_1[b2] - Trits_1[w2];
}

static void indices_column(uint64 b, int & i0, int & i2) {

   uint64 left = b & 0x0C3061830C1860C3; // left 4 files
   uint64 shuffle = (left >> 0) | (left >> 11) | (left >> 22);

   uint64 mask = (1 << Pattern_Size) - 1;
   i0 = (shuffle >>  0) & mask;
   i2 = (shuffle >> 26) & mask;
}

static Bit attacks(const Pos & pos, Side sd) {

   Bit ba = pos.man(sd);
   Bit be = pos.empty();

   uint64 t = 0;

   t |= (ba >> J1) & (be << J1);
   t |= (ba >> I1) & (be << I1);
   t |= (ba << I1) & (be >> I1);
   t |= (ba << J1) & (be >> J1);

   if (var::Variant == var::Frisian) {
      t |= (ba >> L1) & (be << L1);
      t |= (ba >> K1) & (be << K1);
      t |= (ba << K1) & (be >> K1);
      t |= (ba << L1) & (be >> L1);
   }

   return bit::Squares & t;
}

// PST eval for weak levels

static Score eval_pst(const Pos & pos) {

   Side atk = pos.turn();
   Side def = side_opp(atk);

   int sc = 0;

   sc += material(pos, atk) - material(pos, def);
   sc += pst(pos, atk) - pst(pos, def);

   return score::make(sc);
}

static int material(const Pos & pos, Side sd) {
   switch (var::Variant) {
   case var::BT:       return bit::count(pos.man(sd)) * 138;
   case var::Losing:   return bit::count(pos.man(sd)) * 20 + bit::count(pos.king(sd)) * 68;
   case var::Frisian:  return bit::count(pos.man(sd)) * 168 + bit::count(pos.king(sd)) * 423;
   case var::Normal:   return bit::count(pos.man(sd)) * 91  + bit::count(pos.king(sd)) * 238;
   default:            return bit::count(pos.man(sd)) * 100 + bit::count(pos.king(sd)) * 300;
   }
}

static int pst(const Pos & pos, Side sd) {

   int sc = 0;

   for (Square sq : pos.man(sd)) {
      if (sd != White) sq = square_opp(sq);
      switch(var::Variant) {
      case var::BT :       sc += PST_B[square_dense(sq)]; break;
      case var::Losing :   sc += PST_L[square_dense(sq)]; break;
      case var::Frisian :  sc += PST_F[square_dense(sq)]; break;
      default :            sc += PST_I[square_dense(sq)]; break;
      }
   }

   return sc;
}

