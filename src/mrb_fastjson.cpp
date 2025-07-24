#include <mruby.h>
#ifdef MRB_UTF8_STRING
extern "C" {
#include <mruby/internal.h>    /* for mrb_utf8len()/mrb_utf8_strlen() */
}
#endif
#include <mruby/class.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <mruby/string.h>
#include <mruby/numeric.h>
#include <simdjson.h>
#include <mruby/fast_json.h>

using namespace simdjson;

// Forward declarations
static mrb_value mrb_json_parse(mrb_state *mrb, mrb_value self);
static mrb_value convert_element(mrb_state *mrb, dom::element el);
static mrb_value convert_array(mrb_state *mrb, dom::element arr_el);
static mrb_value convert_object(mrb_state *mrb, dom::element obj_el);

static void raise_simdjson_error(mrb_state *mrb, simdjson::error_code code) {
  const char *msg = error_message(code);

  switch (code) {
    case simdjson::UNCLOSED_STRING:       mrb_raise(mrb, E_JSON_UNCLOSED_STRING_ERROR, msg); break;
    case simdjson::STRING_ERROR:          mrb_raise(mrb, E_JSON_STRING_ERROR, msg); break;
    case simdjson::TAPE_ERROR:            mrb_raise(mrb, E_JSON_TAPE_ERROR, msg); break;
    case simdjson::DEPTH_ERROR:           mrb_raise(mrb, E_JSON_DEPTH_ERROR, msg); break;
    case simdjson::MEMALLOC:              mrb_raise(mrb, E_JSON_MEMALLOC_ERROR, msg); break;
    case simdjson::CAPACITY:              mrb_raise(mrb, E_JSON_CAPACITY_ERROR, msg); break;
    case simdjson::NUMBER_ERROR:          mrb_raise(mrb, E_JSON_NUMBER_ERROR, msg); break;
    case simdjson::UTF8_ERROR:            mrb_raise(mrb, E_JSON_UTF8_ERROR, msg); break;
    case simdjson::UNEXPECTED_ERROR:      mrb_raise(mrb, E_JSON_UNEXPECTED_ERROR, msg); break;
    case simdjson::EMPTY:                 mrb_raise(mrb, E_JSON_EMPTY_INPUT_ERROR, msg); break;
    case simdjson::INCORRECT_TYPE:        mrb_raise(mrb, E_JSON_INCORRECT_TYPE_ERROR, msg); break;
    case simdjson::NO_SUCH_FIELD:         mrb_raise(mrb, E_JSON_NO_SUCH_FIELD_ERROR, msg); break;
    case simdjson::UNSUPPORTED_ARCHITECTURE: mrb_raise(mrb, E_JSON_UNSUPPORTED_ARCHITECTURE_ERROR, msg); break;
    default:                              mrb_raise(mrb, E_JSON_PARSER_ERROR, msg); break;
  }
}

static mrb_value
mrb_json_parse(mrb_state *mrb, mrb_value self)
{
  mrb_value str;
  mrb_get_args(mrb, "S", &str);

  const char *cstr = RSTRING_PTR(str);
  size_t len       = RSTRING_LEN(str);

  dom::parser parser;
  auto result = parser.parse(cstr, len);

  if (result.error() != SUCCESS) {
    raise_simdjson_error(mrb, result.error());
  }

  return convert_element(mrb, result.value());
}

static mrb_value
convert_element(mrb_state *mrb, dom::element el)
{
  switch (el.type()) {
    case dom::element_type::ARRAY:
      return convert_array(mrb, el);

    case dom::element_type::OBJECT:
      return convert_object(mrb, el);

    case dom::element_type::INT64:
      return mrb_int_value(mrb, el.get<int64_t>());

    case dom::element_type::UINT64:
      return mrb_int_value(mrb, el.get<uint64_t>());

    case dom::element_type::DOUBLE:
      return mrb_float_value(mrb, double(el));

    case dom::element_type::STRING: {
      std::string_view sv(el);
      return mrb_str_new(mrb, sv.data(), sv.size());
    }

    case dom::element_type::BOOL:
      return mrb_bool_value(el.get_bool());

    case dom::element_type::NULL_VALUE:
      return mrb_nil_value();
  }

  // should never happen
  mrb_raise(mrb, E_RUNTIME_ERROR, "[BUG] unexpected JSON type");
  return mrb_nil_value();
}

static mrb_value
convert_array(mrb_state *mrb, dom::element arr_el)
{
  mrb_value ary = mrb_ary_new(mrb);
  int arena_index = mrb_gc_arena_save(mrb);
  for (dom::element item : arr_el) {
    mrb_ary_push(mrb, ary, convert_element(mrb, item));
    mrb_gc_arena_restore(mrb, arena_index);
  }
  return ary;
}

static mrb_value
convert_object(mrb_state *mrb, dom::element obj_el)
{
  mrb_value hash = mrb_hash_new(mrb);
  int arena_index = mrb_gc_arena_save(mrb);
  for (auto &kv : dom::object(obj_el)) {
    std::string_view key_sv(kv.key);
    mrb_value key = mrb_str_new(mrb, key_sv.data(), key_sv.size());
    mrb_hash_set(mrb,
                 hash,
                 key,
                 convert_element(mrb, kv.value));
    mrb_gc_arena_restore(mrb, arena_index);
  }
  return hash;
}

#include <array>

static const std::array<const char*, 256> JSON_ESCAPE_MAPPING = [](){
  std::array<const char*,256> t{};
  t['"']  = "\\\"";
  t['\\'] = "\\\\";
  t['/']  = "\\/";
  t['\b'] = "\\b";
  t['\f'] = "\\f";
  t['\n'] = "\\n";
  t['\r'] = "\\r";
  t['\t'] = "\\t";
  return t;
}();


static mrb_value
mrb_json_escape_string(mrb_state *mrb, const mrb_value str)
{
  const char *src = RSTRING_PTR(str);
  mrb_int    len = RSTRING_LEN(str);
  mrb_int    cap;

  /* safe-capacity = len*6 + 2, with overflow checks */
  if (mrb_int_mul_overflow(len, 6, &cap) ||
      mrb_int_add_overflow(cap, 2, &cap)) {
    mrb_raise(mrb, E_RANGE_ERROR, "string too large to escape");
  }

  mrb_value result = mrb_str_new_capa(mrb, cap);
  mrb_str_cat_lit(mrb, result, "\"");

#ifdef MRB_UTF8_STRING
  /* use mruby’s UTF-8 helper to walk codepoints */
  static const char hex_digits[] = "0123456789ABCDEF";
  const char *p   = src;
  const char *end = src + len;

  while (p < end) {
    mrb_int w = mrb_utf8len(p, end);
    if (w < 1) w = 1;

    if (w == 1) {
      unsigned char ch = (unsigned char)*p;
      const char *esc = JSON_ESCAPE_MAPPING[ch];

      if (esc) {
        /* two-byte JSON escape */
        mrb_str_cat(mrb, result, esc, 2);
      }
      else if (ch < 0x20) {
        /* control char → \u00XX */
        char buf[6] = {'\\','u','0','0', 0, 0};
        buf[4] = hex_digits[(ch >> 4) & 0xF];
        buf[5] = hex_digits[ch & 0xF];
        mrb_str_cat(mrb, result, buf, 6);
      }
      else {
        /* printable ASCII */
        mrb_str_cat(mrb, result, (const char*)&ch, 1);
      }
    }
    else {
      /* multi-byte UTF-8 → pass through unmodified */
      mrb_str_cat(mrb, result, p, w);
    }

    p += w;
  }

#else  /* no UTF-8 support: simple byte-wise loop */
  for (mrb_int i = 0; i < len; i++) {
    unsigned char ch = (unsigned char)src[i];
    const char *esc  = JSON_ESCAPE_MAPPING[ch];
    if (esc) {
      mrb_str_cat(mrb, result, esc, 2);
    }
    else {
      mrb_str_cat(mrb, result, (const char*)&ch, 1);
    }
  }
#endif

  mrb_str_cat_lit(mrb, result, "\"");
  return result;
}

mrb_value
mrb_json_dump_mrb_obj(mrb_state *mrb, const mrb_value obj)
{
  switch (mrb_type(obj)) {
    case MRB_TT_FALSE:
      if (!mrb_integer(obj)) {
        return mrb_str_new_lit(mrb, "null");
      } else {
        return mrb_str_new_lit(mrb, "false");
      }
    case MRB_TT_TRUE:
      return mrb_str_new_lit(mrb, "true");
    case MRB_TT_SYMBOL:
      return mrb_json_escape_string(mrb, mrb_sym_str(mrb, mrb_symbol(obj)));
    case MRB_TT_FLOAT:
      return mrb_float_to_str(mrb, obj, NULL);
    case MRB_TT_INTEGER:
      return mrb_integer_to_str(mrb, obj, 10);
    case MRB_TT_CLASS:
    case MRB_TT_MODULE:
      return mrb_json_escape_string(mrb, mrb_class_path(mrb, mrb_class_ptr(obj)));
    case MRB_TT_ARRAY:
      return mrb_json_dump_mrb_array(mrb, obj);
    case MRB_TT_HASH:
      return mrb_json_dump_mrb_hash(mrb, obj);
    case MRB_TT_STRING:
      return mrb_json_escape_string(mrb, obj);
    default:
      return mrb_json_escape_string(mrb, mrb_obj_as_string(mrb, obj));
  }
}


mrb_value
mrb_json_dump_mrb_array(mrb_state *mrb, const mrb_value array)
{
  const mrb_int len = RARRAY_LEN(array);
  mrb_value result = mrb_str_new_capa(mrb, 4095 - sizeof(struct RString));
  const int arena_index = mrb_gc_arena_save(mrb);
  mrb_str_cat_lit(mrb, result, "[");
  for (mrb_int i = 0; i < len; i++) {
    const mrb_value val = mrb_ary_ref(mrb, array, i);
    mrb_str_append(mrb, result, mrb_json_dump_mrb_obj(mrb, val));
    if (i < len - 1) {
      mrb_str_cat_lit(mrb, result, ",");
    }
    mrb_gc_arena_restore(mrb, arena_index);
  }
  mrb_str_cat_lit(mrb, result, "]");

  return mrb_str_resize(mrb, result, RSTRING_LEN(result));
}

mrb_value
mrb_json_dump_mrb_hash(mrb_state *mrb, const mrb_value hash)
{
  const mrb_value keys = mrb_hash_keys(mrb, hash);
  const mrb_int len = RARRAY_LEN(keys);
  mrb_value result = mrb_str_new_capa(mrb, 4095 - sizeof(struct RString));
  const int arena_index = mrb_gc_arena_save(mrb);
  mrb_str_cat_lit(mrb, result, "{");
  for (mrb_int i = 0; i < len; i++) {
    const mrb_value key = mrb_ary_ref(mrb, keys, i);
    mrb_str_append(mrb, result, mrb_json_dump_mrb_obj(mrb, key));
    mrb_str_cat_lit(mrb, result, ":");
    const mrb_value val = mrb_hash_get(mrb, hash, key);
    mrb_str_append(mrb, result, mrb_json_dump_mrb_obj(mrb, val));
    if (i < len - 1) {
      mrb_str_cat_lit(mrb, result, ",");
    }
    mrb_gc_arena_restore(mrb, arena_index);
  }
  mrb_str_cat_lit(mrb, result, "}");

  return mrb_str_resize(mrb, result, RSTRING_LEN(result));
}

static mrb_value
mrb_json_dump_m(mrb_state *mrb, mrb_value self)
{
  mrb_value obj;
  mrb_get_args(mrb, "o", &obj);

  // Convert the object to JSON string
  return mrb_json_dump_mrb_obj(mrb, obj);
}

extern "C" void
mrb_mruby_fast_json_gem_init(mrb_state *mrb)
{
  struct RClass *json_mod    = mrb_define_module(mrb, "JSON");
  struct RClass *json_error  = mrb_define_class_under(mrb, json_mod, "ParserError", mrb->eStandardError_class);

#define DEFINE_JSON_ERROR(NAME) \
  mrb_define_class_under(mrb, json_mod, #NAME "Error", json_error)
  // Specific error classes
  DEFINE_JSON_ERROR(Tape);
  DEFINE_JSON_ERROR(String);
  DEFINE_JSON_ERROR(UnclosedString);
  DEFINE_JSON_ERROR(MemoryAllocation);
  DEFINE_JSON_ERROR(Depth);
  DEFINE_JSON_ERROR(UTF8);
  DEFINE_JSON_ERROR(Number);
  DEFINE_JSON_ERROR(Capacity);
  DEFINE_JSON_ERROR(IncorrectType);
  DEFINE_JSON_ERROR(EmptyInput);

  mrb_define_class_method(mrb, json_mod, "parse", mrb_json_parse, MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, json_mod, "dump", mrb_json_dump_m, MRB_ARGS_REQ(1));
}

extern "C" void
mrb_mruby_fast_json_gem_final(mrb_state *mrb) {}
