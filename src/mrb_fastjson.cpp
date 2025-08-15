#include <mruby.h>
#include <mruby/class.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <mruby/string.h>
#include <mruby/numeric.h>
#include <mruby/fast_json.h>
#include <mruby/cpp_helpers.hpp>
#include <mruby/num_helpers.hpp>
#include <mruby/presym.h>
#include <mruby/object.h>
#include <mruby/branch_pred.h>
#ifndef MRB_DEBUG
#define NDEBUG
#define __OPTIMIZE__=1
#endif
#include <simdjson.h>


using namespace simdjson;

static mrb_value convert_element(mrb_state *mrb, dom::element el, mrb_bool symbolize_names);
static mrb_value convert_array(mrb_state *mrb, dom::element arr_el, mrb_bool symbolize_names);
static mrb_value convert_object(mrb_state *mrb, dom::element obj_el, mrb_bool symbolize_names);

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

static padded_string_view
simdjson_safe_view_from_mrb_string(mrb_state* mrb, mrb_value str, padded_string& jsonbuffer) {
  const char* cstr = RSTRING_PTR(str);
  size_t len = RSTRING_LEN(str);

  if (mrb_frozen_p(mrb_obj_ptr(str))) {
    jsonbuffer = padded_string(cstr, len); // copies & pads safely
    return jsonbuffer;
  }

  size_t required = len + SIMDJSON_PADDING;
  if (RSTRING_CAPA(str) < required) {
    mrb_str_resize(mrb, str, required); // resize backing buffer
    RSTR_SET_LEN(RSTRING(str), len);     // keep logical length unchanged
    cstr = RSTRING_PTR(str);             // refresh pointer after resize
  }

  return padded_string_view(cstr, len, required);
}

static mrb_value
mrb_json_parse(mrb_state *mrb, mrb_value self)
{
  mrb_value str;
  mrb_value kw_values[1];
  mrb_sym kw_names[] = { MRB_SYM(symbolize_names) };
  mrb_kwargs kwargs = {
    1,                // num: number of keywords
    0,                // required: none required
    kw_names,
    kw_values,
    NULL
  };

  mrb_get_args(mrb, "S:", &str, &kwargs);

  // Fallback default
  mrb_bool symbolize_names = FALSE;
  if (!mrb_undef_p(kw_values[0])) {
    symbolize_names = mrb_bool(kw_values[0]); // cast to mrb_bool
  }

  dom::parser parser;
  padded_string jsonbuffer;
  auto view = simdjson_safe_view_from_mrb_string(mrb, str, jsonbuffer);
  auto result = parser.parse(view);

  if (unlikely(result.error() != SUCCESS)) {
    raise_simdjson_error(mrb, result.error());
  }

  return convert_element(mrb, result.value(), symbolize_names);
}

static mrb_value
convert_element(mrb_state *mrb, dom::element el, mrb_bool symbolize_names)
{
  switch (el.type()) {
    case dom::element_type::ARRAY:
      return convert_array(mrb, el, symbolize_names);

    case dom::element_type::OBJECT:
      return convert_object(mrb, el, symbolize_names);

    case dom::element_type::INT64:
      return mrb_convert_number(mrb, static_cast<int64_t>(el.get<int64_t>()));

    case dom::element_type::UINT64:
      return mrb_convert_number(mrb, static_cast<uint64_t>(el.get<uint64_t>()));

    case dom::element_type::DOUBLE:
      return mrb_convert_number(mrb, static_cast<double>(el.get<double>()));

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
  return mrb_undef_value();
}

static mrb_value
convert_array(mrb_state *mrb, dom::element arr_el, mrb_bool symbolize_names)
{
  mrb_value ary = mrb_ary_new(mrb);
  int arena_index = mrb_gc_arena_save(mrb);
  for (dom::element item : arr_el) {
    mrb_ary_push(mrb, ary, convert_element(mrb, item, symbolize_names));
    mrb_gc_arena_restore(mrb, arena_index);
  }
  return ary;
}

using KeyConverterFn = mrb_value(*)(mrb_state*, std::string_view);

static mrb_value
convert_key_as_str(mrb_state* mrb, std::string_view sv) {
  return mrb_str_new(mrb, sv.data(), sv.size());
}

static mrb_value
convert_key_as_sym(mrb_state* mrb, std::string_view sv) {
  return mrb_symbol_value(mrb_intern(mrb, sv.data(), sv.size()));
}

static mrb_value
convert_object(mrb_state *mrb, dom::element obj_el, mrb_bool symbolize_names)
{
  mrb_value hash = mrb_hash_new(mrb);
  int arena_index = mrb_gc_arena_save(mrb);
  KeyConverterFn convert_key = symbolize_names ? convert_key_as_sym : convert_key_as_str;

  for (auto& kv : dom::object(obj_el)) {
    mrb_value key = convert_key(mrb, kv.key);
    mrb_value val = convert_element(mrb, kv.value, symbolize_names);
    mrb_hash_set(mrb, hash, key, val);
    mrb_gc_arena_restore(mrb, arena_index);
  }

  return hash;
}

static inline void append_u00XX(std::string& out, unsigned char c) {
    static constexpr char hex[] = "0123456789ABCDEF";
    out.push_back('\\');
    out.push_back('u');
    out.push_back('0');
    out.push_back('0');
    out.push_back(hex[(c >> 4) & 0xF]);
    out.push_back(hex[c & 0xF]);
}

// String escaping helper
static void json_escape_string(mrb_state* mrb, mrb_value s, std::string& out) {
    const unsigned char* p = (const unsigned char*)RSTRING_PTR(s);
    mrb_int n = RSTRING_LEN(s);

    out.push_back('"');
    for (mrb_int i = 0; i < n; ++i) {
        unsigned char c = p[i];
        switch (c) {
            case '\"': out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\b': out.append("\\b");  break;
            case '\f': out.append("\\f");  break;
            case '\n': out.append("\\n");  break;
            case '\r': out.append("\\r");  break;
            case '\t': out.append("\\t");  break;
default:
  if (c < 0x20) {
    append_u00XX(out, c);
  } else {
    out.push_back((char)c);
  }
  break;
        }
    }
    out.push_back('"');
}

// Context struct for hash_foreach
struct DumpHashCtx {
    mrb_state* mrb;
    std::string* out;
    bool first;
};

// Forward declare encoder
static void json_encode(mrb_state* mrb, mrb_value v, std::string& out);

// Hash foreach callback
static int dump_hash_cb(mrb_state* mrb, mrb_value key, mrb_value val, void* data) {
    auto* ctx = static_cast<DumpHashCtx*>(data);

    if (!ctx->first) ctx->out->push_back(',');
    ctx->first = false;

    json_encode(mrb, key, *ctx->out);
    ctx->out->push_back(':');
    json_encode(mrb, val, *ctx->out);

    return 0; // continue iteration
}

// Main encoder
static void json_encode(mrb_state* mrb, mrb_value v, std::string& out) {
    switch (mrb_type(v)) {
        case MRB_TT_FALSE:
            if (mrb_nil_p(v)) {
                out.append("null");
            } else {
                out.append("false");
            }
            break;

        case MRB_TT_TRUE:
            out.append("true");
            break;

        case MRB_TT_SYMBOL:
            json_escape_string(mrb, mrb_sym_str(mrb, mrb_symbol(v)), out);
            break;

        case MRB_TT_STRING:
            json_escape_string(mrb, v, out);
            break;

        case MRB_TT_FLOAT: {
            mrb_value s = mrb_float_to_str(mrb, v, NULL);
            out.append(RSTRING_PTR(s), RSTRING_LEN(s));
            break;
        }

        case MRB_TT_INTEGER: {
            char buf[MRB_INT_BIT + 1];
            char* p = mrb_int_to_cstr(buf, sizeof(buf), mrb_integer(v), 10);
            out.append(p);
            break;
        }

        case MRB_TT_ARRAY: {
            out.push_back('[');
            mrb_int n = RARRAY_LEN(v);
            for (mrb_int i = 0; i < n; ++i) {
                if (i) out.push_back(',');
                json_encode(mrb, mrb_ary_ref(mrb, v, i), out);
            }
            out.push_back(']');
            break;
        }

        case MRB_TT_HASH: {
            out.push_back('{');
            DumpHashCtx ctx{mrb, &out, true};
            mrb_hash_foreach(mrb, mrb_hash_ptr(v), dump_hash_cb, &ctx);
            out.push_back('}');
            break;
        }

        default:
            json_escape_string(mrb, mrb_obj_as_string(mrb, v), out);
            break;
    }
}

// Public dump entrypoint for any mrb_value
MRB_API mrb_value mrb_json_dump_mrb_obj(mrb_state* mrb, mrb_value obj) {
    std::string out;
    json_encode(mrb, obj, out);
    return mrb_str_new(mrb, out.data(), out.size());
}

static mrb_value
mrb_json_dump_m(mrb_state *mrb, mrb_value self)
{
  mrb_value obj;
  mrb_get_args(mrb, "o", &obj);

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

  mrb_define_class_method(mrb, json_mod, "parse", mrb_json_parse, MRB_ARGS_REQ(1)|MRB_ARGS_KEY(1, 0));
  mrb_define_class_method(mrb, json_mod, "dump", mrb_json_dump_m, MRB_ARGS_REQ(1));
}

extern "C" void
mrb_mruby_fast_json_gem_final(mrb_state *mrb) {}
