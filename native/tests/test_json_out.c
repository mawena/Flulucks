#include <stdint.h>
#include <string.h>

#include "fluluks/error.h"
#include "json_out.h"
#include "tt.h"

/* Sortie d'une chaîne seule. */
static const char *str_seule(fl_json *j, const char *s, size_t n) {
  fl_json_reset(j);
  fl_json_str(j, s, n);
  return j->err == FL_OK ? j->buf : "(err)";
}

static void test_guillemets_et_controle(void) {
  fl_json j;
  fl_json_init(&j);
  TT_ASSERT_STR_EQ(str_seule(&j, "a\"b\\c", 5), "\"a\\\"b\\\\c\"");
  TT_ASSERT_STR_EQ(str_seule(&j, "\n\t\x01\x1f", 4), "\"\\u000a\\u0009\\u0001\\u001f\"");
  TT_ASSERT_STR_EQ(str_seule(&j, "x\0y", 3), "\"x\\u0000y\"");
  TT_ASSERT_STR_EQ(str_seule(&j, "\x7f/", 2), "\"\x7f/\""); /* DEL et / restent tels quels */
  TT_ASSERT_STR_EQ(str_seule(&j, "", 0), "\"\"");
  fl_json_free(&j);
}

static void test_utf8_invalide(void) {
  fl_json j;
  fl_json_init(&j);
#define FFFD "\xEF\xBF\xBD"
  TT_ASSERT_STR_EQ(str_seule(&j, "a\xff" "b", 3), "\"a" FFFD "b\"");        /* octet interdit */
  TT_ASSERT_STR_EQ(str_seule(&j, "\x80", 1), "\"" FFFD "\"");               /* suite isolée */
  TT_ASSERT_STR_EQ(str_seule(&j, "\xc3", 1), "\"" FFFD "\"");               /* tronquée en fin */
  TT_ASSERT_STR_EQ(str_seule(&j, "\xc3z", 2), "\"" FFFD "z\"");             /* tronquée */
  TT_ASSERT_STR_EQ(str_seule(&j, "\xc0\xaf", 2), "\"" FFFD FFFD "\"");      /* forme trop longue */
  TT_ASSERT_STR_EQ(str_seule(&j, "\xe0\x80\xaf", 3), "\"" FFFD FFFD FFFD "\"");
  TT_ASSERT_STR_EQ(str_seule(&j, "\xed\xa0\x80", 3), "\"" FFFD FFFD FFFD "\""); /* surrogate */
  TT_ASSERT_STR_EQ(str_seule(&j, "\xf4\x90\x80\x80", 4), "\"" FFFD FFFD FFFD FFFD "\""); /* > U+10FFFF */
  TT_ASSERT_STR_EQ(str_seule(&j, "\xf0\x9f\x98", 3), "\"" FFFD "\"");       /* émoji tronqué : 1 seul */
  TT_ASSERT_STR_EQ(str_seule(&j, "\xe2\x98z", 3), "\"" FFFD "z\"");
#undef FFFD
  fl_json_free(&j);
}

static void test_utf8_valide_conserve(void) {
  fl_json j;
  fl_json_init(&j);
  const char *nom = "accentué é ☀.txt";
  char attendu[64];
  snprintf(attendu, sizeof attendu, "\"%s\"", nom);
  TT_ASSERT_STR_EQ(str_seule(&j, nom, strlen(nom)), attendu);
  const char *emoji = "\xf0\x9f\x98\x80 \xf4\x8f\xbf\xbf \xef\xbf\xbd"; /* U+1F600, U+10FFFF, U+FFFD */
  snprintf(attendu, sizeof attendu, "\"%s\"", emoji);
  TT_ASSERT_STR_EQ(str_seule(&j, emoji, strlen(emoji)), attendu);
  fl_json_free(&j);
}

static void test_structure(void) {
  fl_json j;
  fl_json_init(&j);
  fl_json_obj_begin(&j);
  fl_json_key(&j, "a");
  fl_json_u64(&j, UINT64_MAX);
  fl_json_key(&j, "b");
  fl_json_arr_begin(&j);
  fl_json_i64(&j, INT64_MIN);
  fl_json_bool(&j, true);
  fl_json_obj_begin(&j);
  fl_json_obj_end(&j);
  fl_json_arr_begin(&j);
  fl_json_arr_end(&j);
  fl_json_cstr(&j, "x");
  fl_json_arr_end(&j);
  fl_json_key(&j, "c");
  fl_json_bool(&j, false);
  fl_json_obj_end(&j);
  TT_ASSERT_EQ(j.err, FL_OK);
  TT_ASSERT_STR_EQ(j.buf, "{\"a\":18446744073709551615,\"b\":[-9223372036854775808,true,{},[],\"x\"],\"c\":false}");
  TT_ASSERT_EQ(j.len, strlen(j.buf));
  fl_json_error(&j, FL_EBADPASS);
  TT_ASSERT_STR_EQ(j.buf, "{\"err\":-5}");
  fl_json_free(&j);
}

static void test_limite(void) {
  fl_json j;
  fl_json_init(&j);
  static char gros[1u << 20];
  memset(gros, 'a', sizeof gros);
  fl_json_arr_begin(&j);
  for (int i = 0; i < 70 && j.err == FL_OK; i++) fl_json_str(&j, gros, sizeof gros);
  TT_ASSERT_EQ(j.err, FL_ENOMEM);
  TT_ASSERT(j.len <= FL_JSON_MAX);
  fl_json_error(&j, FL_EIO);
  TT_ASSERT_EQ(j.err, FL_OK);
  TT_ASSERT_STR_EQ(j.buf, "{\"err\":-1}");
  fl_json_free(&j);
}

static void test_limite_dediee(void) {
  fl_json j;
  fl_json_init(&j);
  fl_json_set_max(&j, FL_JSON_LIST_MAX);
  TT_ASSERT_EQ(j.max, 16u * 1024u * 1024u);
  static char gros[1u << 20];
  memset(gros, 'b', sizeof gros);
  fl_json_arr_begin(&j);
  int n = 0;
  for (; n < 70 && j.err == FL_OK; n++) fl_json_str(&j, gros, sizeof gros);
  TT_ASSERT_EQ(j.err, FL_ENOMEM);
  TT_ASSERT_EQ(n, 16); /* 15 chaînes de 1 Mio tiennent, la 16e déborde */
  TT_ASSERT(j.len <= FL_JSON_LIST_MAX);
  fl_json_error(&j, FL_ENOMEM); /* la réponse d'erreur passe, le plafond reste */
  TT_ASSERT_EQ(j.err, FL_OK);
  TT_ASSERT_STR_EQ(j.buf, "{\"err\":-3}");
  TT_ASSERT_EQ(j.max, FL_JSON_LIST_MAX);
  fl_json_set_max(&j, (size_t)FL_JSON_MAX * 2); /* borné au plafond global */
  TT_ASSERT_EQ(j.max, FL_JSON_MAX);
  fl_json_free(&j);
}

int main(void) {
  TT_RUN(test_guillemets_et_controle);
  TT_RUN(test_utf8_invalide);
  TT_RUN(test_utf8_valide_conserve);
  TT_RUN(test_structure);
  TT_RUN(test_limite);
  TT_RUN(test_limite_dediee);
  TT_MAIN_END();
}
