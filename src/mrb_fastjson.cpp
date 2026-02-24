#include <mruby.h>
#include <mruby/array.h>
#include <mruby/branch_pred.h>
#include <mruby/class.h>
#include <mruby/cpp_helpers.hpp>
#include <mruby/fast_json.h>
#include <mruby/hash.h>
#include <mruby/num_helpers.hpp>
#include <mruby/numeric.h>
#include <mruby/object.h>
#include <mruby/presym.h>
#include <mruby/string.h>
#include <mruby/variable.h>
#include <mruby/error.h>
#include <mruby/data.h>
MRB_BEGIN_DECL
#include <mruby/internal.h>
MRB_END_DECL
#include <string_view>
#ifndef SIMDJSON_THREADS_ENABLED
#define SIMDJSON_THREADS_ENABLED
#endif
#include <simdjson.h>

using namespace simdjson;

#ifdef _WIN32
#include <windows.h>
#include <sysinfoapi.h>
#else
#include <unistd.h>
#endif
#include <cstdio>

static long pagesize;

// Returns true if the buffer + len + simdjson::SIMDJSON_PADDING crosses the
// page boundary.
static bool need_allocation(const char* buf,
                    mrb_int len,
                    mrb_int capa)
{
#ifdef MRB_DEBUG
  return true; // always allocate padded_string in debug mode to detect issues
#endif
  uintptr_t end = reinterpret_cast<uintptr_t>(buf + len - 1);
  uintptr_t offset = end % pagesize;
  if (likely(offset + SIMDJSON_PADDING < static_cast<uintptr_t>(pagesize))) {
      return false;
  }

  if (capa >= len + SIMDJSON_PADDING) {
    return false; // safe
  }
  return true; // must allocate padded_string
}

static padded_string_view
simdjson_safe_view_from_mrb_string(mrb_state *mrb, mrb_value str,
                                   padded_string &jsonbuffer) {
  mrb_int len = RSTRING_LEN(str);
  if (mrb_test(mrb_iv_get(mrb, mrb_obj_value(mrb_module_get_id(mrb, MRB_SYM(JSON))), MRB_IVSYM(zero_copy_parsing)))) {
    if (likely(!need_allocation(RSTRING_PTR(str), len, RSTRING_CAPA(str)))) {
      str = mrb_obj_freeze(mrb, str);
      return padded_string_view(RSTRING_PTR(str), len, len + SIMDJSON_PADDING);
    }
  }

  if (mrb_frozen_p(mrb_obj_ptr(str))) {
    jsonbuffer = padded_string(RSTRING_PTR(str), len);
    return jsonbuffer;
  }

  // Prevent overflow in len + SIMDJSON_PADDING
  if (unlikely(len > SIZE_MAX - SIMDJSON_PADDING)) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "JSON input too large for padding");
  }

  mrb_int required = len + SIMDJSON_PADDING;

  if (RSTRING_CAPA(str) < required) {
    // grow Ruby string to required bytes (len + padding)
    str = mrb_str_resize(mrb, str, required);
    str = mrb_obj_freeze(mrb, str);
    // restore logical length to original JSON length
    RSTR_SET_LEN(RSTRING(str), len);
  }

  // capacity is now at least `required`
  return padded_string_view(RSTRING_PTR(str), len, required);
}

static mrb_value convert_array(mrb_state* mrb,
                               const dom::element& arr_el,
                               mrb_bool symbolize_names);

static mrb_value convert_object(mrb_state* mrb,
                                const dom::element& obj_el,
                                mrb_bool symbolize_names);


static mrb_value convert_element(mrb_state *mrb, const dom::element& el,
                                 mrb_bool symbolize_names) {
  using namespace dom;
  switch (el.type()) {
  case element_type::ARRAY:
    return convert_array(mrb, el, symbolize_names);

  case element_type::OBJECT:
    return convert_object(mrb, el, symbolize_names);

  case element_type::INT64:
    return mrb_convert_number(mrb, static_cast<int64_t>(el.get<int64_t>()));

  case element_type::UINT64:
    return mrb_convert_number(mrb, static_cast<uint64_t>(el.get<uint64_t>()));

  case element_type::DOUBLE:
    return mrb_convert_number(mrb, static_cast<double>(el.get<double>()));

  case element_type::STRING: {
    std::string_view sv(el);
    return mrb_str_new(mrb, sv.data(), sv.size());
  }

  case element_type::BOOL:
    return mrb_bool_value(el.get_bool());

  case element_type::NULL_VALUE:
    return mrb_nil_value();
  default:
    mrb_raise(mrb, E_TYPE_ERROR, "unknown JSON type");
    return mrb_undef_value(); // unreachable
  }
}

static mrb_value convert_array(mrb_state *mrb, const dom::element& arr_el,
                               mrb_bool symbolize_names) {
  dom::array arr = arr_el.get_array();
  mrb_value ary = mrb_ary_new_capa(mrb, arr.size());
  int arena_index = mrb_gc_arena_save(mrb);
  for (dom::element item : arr) {
    mrb_ary_push(mrb, ary, convert_element(mrb, item, symbolize_names));
    mrb_gc_arena_restore(mrb, arena_index);
  }
  return ary;
}

using KeyConverterFn = mrb_value (*)(mrb_state *, std::string_view);

static mrb_value convert_key_as_str(mrb_state *mrb, std::string_view sv) {
  return mrb_str_new(mrb, sv.data(), sv.size());
}

static mrb_value convert_key_as_sym(mrb_state *mrb, std::string_view sv) {
  return mrb_symbol_value(mrb_intern(mrb, sv.data(), sv.size()));
}

static mrb_value convert_object(mrb_state *mrb, const dom::element& obj_el,
                                mrb_bool symbolize_names) {
  dom::object obj = obj_el.get_object();

  mrb_value hash = mrb_hash_new_capa(mrb, obj.size());
  int arena_index = mrb_gc_arena_save(mrb);
  KeyConverterFn convert_key =
      symbolize_names ? convert_key_as_sym : convert_key_as_str;

  for (auto &kv : obj) {
    mrb_value key = convert_key(mrb, kv.key);
    mrb_value val = convert_element(mrb, kv.value, symbolize_names);
    mrb_hash_set(mrb, hash, key, val);
    mrb_gc_arena_restore(mrb, arena_index);
  }

  return hash;
}

static void raise_simdjson_error(mrb_state *mrb, const error_code code) {
  const char *msg = error_message(code);

  switch (code) {
  case UNCLOSED_STRING:
    mrb_raise(mrb, E_JSON_UNCLOSED_STRING_ERROR, msg);
    break;
  case STRING_ERROR:
    mrb_raise(mrb, E_JSON_STRING_ERROR, msg);
    break;
  case UNESCAPED_CHARS:
    mrb_raise(mrb, E_JSON_UNESCAPED_CHARS_ERROR, msg);
    break;

  case TAPE_ERROR:
    mrb_raise(mrb, E_JSON_TAPE_ERROR, msg);
    break;
  case DEPTH_ERROR:
    mrb_raise(mrb, E_JSON_DEPTH_ERROR, msg);
    break;
  case INCOMPLETE_ARRAY_OR_OBJECT:
    mrb_raise(mrb, E_JSON_INCOMPLETE_ARRAY_OR_OBJECT_ERROR, msg);
    break;
  case TRAILING_CONTENT:
    mrb_raise(mrb, E_JSON_TRAILING_CONTENT_ERROR, msg);
    break;

  case MEMALLOC:
    mrb_exc_raise(mrb, mrb_obj_value(mrb->nomem_err));
    break;
  case CAPACITY:
    mrb_raise(mrb, E_JSON_CAPACITY_ERROR, msg);
    break;
  case OUT_OF_CAPACITY:
    mrb_raise(mrb, E_JSON_OUT_OF_CAPACITY_ERROR, msg);
    break;
  case INSUFFICIENT_PADDING:
    mrb_raise(mrb, E_JSON_INSUFFICIENT_PADDING_ERROR, msg);
    break;

  case NUMBER_ERROR:
    mrb_raise(mrb, E_JSON_NUMBER_ERROR, msg);
    break;
  case BIGINT_ERROR:
    mrb_raise(mrb, E_JSON_BIGINT_ERROR, msg);
    break;
  case NUMBER_OUT_OF_RANGE:
    mrb_raise(mrb, E_JSON_NUMBER_OUT_OF_RANGE_ERROR, msg);
    break;

  case T_ATOM_ERROR:
    mrb_raise(mrb, E_JSON_T_ATOM_ERROR, msg);
    break;
  case F_ATOM_ERROR:
    mrb_raise(mrb, E_JSON_F_ATOM_ERROR, msg);
    break;
  case N_ATOM_ERROR:
    mrb_raise(mrb, E_JSON_N_ATOM_ERROR, msg);
    break;

  case UTF8_ERROR:
    mrb_raise(mrb, E_JSON_UTF8_ERROR, msg);
    break;

  case EMPTY:
    mrb_raise(mrb, E_JSON_EMPTY_INPUT_ERROR, msg);
    break;
  case UNINITIALIZED:
    mrb_raise(mrb, E_JSON_UNINITIALIZED_ERROR, msg);
    break;
  case PARSER_IN_USE:
    mrb_raise(mrb, E_JSON_PARSER_IN_USE_ERROR, msg);
    break;
  case SCALAR_DOCUMENT_AS_VALUE:
    mrb_raise(mrb, E_JSON_SCALAR_DOCUMENT_AS_VALUE_ERROR, msg);
    break;

  case INCORRECT_TYPE:
    mrb_raise(mrb, E_TYPE_ERROR, msg);
    break;
  case NO_SUCH_FIELD:
    mrb_raise(mrb, E_JSON_NO_SUCH_FIELD_ERROR, msg);
    break;
  case INDEX_OUT_OF_BOUNDS:
    mrb_raise(mrb, E_INDEX_ERROR, msg);
    break;
  case OUT_OF_BOUNDS:
    mrb_raise(mrb, E_JSON_OUT_OF_BOUNDS_ERROR, msg);
    break;
  case OUT_OF_ORDER_ITERATION:
    mrb_raise(mrb, E_JSON_OUT_OF_ORDER_ITERATION_ERROR, msg);
    break;

  case IO_ERROR:
    mrb_raise(mrb, E_JSON_IO_ERROR, msg);
    break;
  case INVALID_JSON_POINTER:
    mrb_raise(mrb, E_JSON_INVALID_JSON_POINTER_ERROR, msg);
    break;
  case INVALID_URI_FRAGMENT:
    mrb_raise(mrb, E_JSON_INVALID_URI_FRAGMENT_ERROR, msg);
    break;

  case UNSUPPORTED_ARCHITECTURE:
    mrb_raise(mrb, E_JSON_UNSUPPORTED_ARCHITECTURE_ERROR, msg);
    break;
  case UNEXPECTED_ERROR:
    mrb_raise(mrb, E_JSON_UNEXPECTED_ERROR, msg);
    break;

  default:
    mrb_raise(mrb, E_JSON_PARSER_ERROR, msg);
    break;
  }
}

MRB_API mrb_value mrb_json_parse(mrb_state *mrb, mrb_value str,
                                 mrb_bool symbolize_names) {
  dom::parser parser;
  padded_string jsonbuffer;
  auto view = simdjson_safe_view_from_mrb_string(mrb, str, jsonbuffer);
  auto result = parser.parse(view);

  if (unlikely(result.error() != SUCCESS)) {
    raise_simdjson_error(mrb, result.error());
  }

  return convert_element(mrb, result.value(), symbolize_names);
}

static mrb_value mrb_json_parse_m(mrb_state *mrb, mrb_value self) {
  mrb_value str;
  mrb_value kw_values[1] = {
      mrb_undef_value()}; // default value for symbolize_names
  mrb_sym kw_names[] = {MRB_SYM(symbolize_names)};
  mrb_kwargs kwargs = {1, // num: number of keywords
                       0, // required: none required
                       kw_names, kw_values, NULL};

  mrb_get_args(mrb, "S:", &str, &kwargs);

  // Fallback default
  mrb_bool symbolize_names = FALSE;
  if (!mrb_undef_p(kw_values[0])) {
    symbolize_names = mrb_bool(kw_values[0]); // cast to mrb_bool
  }

  return mrb_json_parse(mrb, str, symbolize_names);
}

#ifndef MRB_STR_LENGTH_MAX
#define MRB_STR_LENGTH_MAX 1048576
#endif

MRB_CPP_DEFINE_TYPE(ondemand::parser, ondemand_parser);
MRB_CPP_DEFINE_TYPE(padded_string, padded_string);
MRB_CPP_DEFINE_TYPE(padded_string_view, padded_string_view);
MRB_CPP_DEFINE_TYPE(ondemand::document, ondemand_document);

static mrb_value
make_padded_string_view_from_ruby_str(mrb_state *mrb, mrb_value str)
{
  struct RClass *json_mod = mrb_module_get_id(mrb, MRB_SYM(JSON));
  mrb_value zero_copy  = mrb_iv_get(mrb, mrb_obj_value(json_mod), MRB_IVSYM(zero_copy_parsing));

  mrb_int len = RSTRING_LEN(str);

  mrb_value argv[] = {mrb_undef_value(), mrb_undef_value()};
  mrb_int argc = 0;

  if (mrb_test(zero_copy) && likely(!need_allocation(RSTRING_PTR(str), len, RSTRING_CAPA(str)))) {
    argv[0] = mrb_obj_freeze(mrb, str);
    argv[1] = mrb_convert_number(mrb, len + SIMDJSON_PADDING);
    argc = 2;

  } else if (mrb_frozen_p(mrb_obj_ptr(str))) {
    argv[0] = mrb_obj_new(
      mrb,
      mrb_class_get_under_id(mrb, json_mod, MRB_SYM(PaddedString)),
      1, &str
    );
    argc = 1;

  } else if (unlikely(len > SIZE_MAX - SIMDJSON_PADDING)) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "JSON input too large for padding");
  } else {
    mrb_int required = len + SIMDJSON_PADDING;

    if (RSTRING_CAPA(str) < required) {
      str = mrb_str_resize(mrb, str, required);
      str = mrb_obj_freeze(mrb, str);
      RSTR_SET_LEN(RSTRING(str), len);
    }

    argv[0] = str;
    argc = 1;
  }

  return mrb_obj_new(
    mrb,
    mrb_class_get_under_id(mrb, json_mod, MRB_SYM(PaddedStringView)),
    argc, argv
  );
}

static mrb_value
mrb_ondemand_parser_initialize(mrb_state *mrb, mrb_value self)
{
  mrb_int max_capacity = SIMDJSON_MAXSIZE_BYTES;

  mrb_get_args(mrb, "|i", &max_capacity);

  mrb_cpp_new<ondemand::parser>(mrb, self, max_capacity);

  return self;
}

static mrb_value
mrb_ondemand_parser_allocate(mrb_state *mrb, mrb_value self)
{
  mrb_int new_capacity  = MRB_STR_LENGTH_MAX;
  mrb_int new_max_depth = DEFAULT_MAX_DEPTH;
  mrb_get_args(mrb, "|i", &new_capacity, new_max_depth);

  auto code = mrb_cpp_get<ondemand::parser>(mrb, self)->allocate(new_capacity, new_max_depth);
  if (likely(code == SUCCESS)) return self;

  raise_simdjson_error(mrb, code);
  return mrb_undef_value();
}

static mrb_value
mrb_ondemand_parser_iterate(mrb_state *mrb, mrb_value self)
{
  mrb_value arg;
  mrb_get_args(mrb, "S", &arg);

  mrb_value view_obj = make_padded_string_view_from_ruby_str(mrb, arg);
  struct RClass *json_mod = mrb_module_get_id(mrb, MRB_SYM(JSON));
  struct RClass *doc_cls =
    mrb_class_get_under_id(mrb, json_mod, MRB_SYM(Document));
  mrb_value args[] = { view_obj, self };
  return mrb_obj_new(mrb, doc_cls, 2, args);
}

static mrb_value
mrb_padded_string_initialize(mrb_state *mrb, mrb_value self)
{
  mrb_value buf;
  mrb_int argc = mrb_get_args(mrb, "|S", &buf);
  if (argc == 0) {
    mrb_cpp_new<padded_string>(mrb, self);
    return self;
  }

  mrb_cpp_new<padded_string>(
    mrb,
    self,
    RSTRING_PTR(buf),
    RSTRING_LEN(buf)
  );

  return self;
}

static mrb_value
mrb_padded_string_s_load(mrb_state *mrb, mrb_value self)
{
  mrb_value path;
  mrb_get_args(mrb, "S", &path);

  std::string_view sv(RSTRING_PTR(path), RSTRING_LEN(path));

  // 1. Load padded_string from file
  padded_string loaded = padded_string::load(sv);

  // 2. Create Ruby PaddedString object to own the buffer
  struct RClass *json_mod = mrb_module_get_id(mrb, MRB_SYM(JSON));
  struct RClass *ps_class = mrb_class_ptr(self);

  mrb_value ps_obj = mrb_obj_new(mrb, ps_class, 0, NULL);
  auto *ps = mrb_cpp_get<padded_string>(mrb, ps_obj);
  *ps = std::move(loaded);

  // 3. Create padded_string_view from the padded_string
  padded_string_view psv(*ps);

  // 4. Create Ruby PaddedStringView object
  struct RClass *psv_class =
    mrb_class_get_under_id(mrb, json_mod, MRB_SYM(PaddedStringView));

  mrb_value view_obj = mrb_obj_new(mrb, psv_class, 0, NULL);
  auto *view_cpp = mrb_cpp_get<padded_string_view>(mrb, view_obj);
  *view_cpp = std::move(psv);

  // 5. Store the padded_string inside the view for lifetime safety
  mrb_iv_set(mrb, view_obj, MRB_SYM(buf), ps_obj);

  return view_obj;
}

static mrb_value
mrb_padded_string_view_initialize(mrb_state *mrb, mrb_value self)
{
  mrb_value buf = mrb_undef_value();
  mrb_int capa = 0;
  mrb_int argc = mrb_get_args(mrb, "|oi", &buf, &capa);

  if (argc == 0) {
    mrb_cpp_new<padded_string_view>(mrb, self);
    return self;
  }

  if (mrb_string_p(buf)) {
    mrb_cpp_new<padded_string_view>(
        mrb, self,
        RSTRING_PTR(buf),
        RSTRING_LEN(buf),
        argc == 1 ? RSTRING_CAPA(buf) : capa);
  } else {
    auto *ps = mrb_cpp_get<padded_string>(mrb, buf);
    auto psv = padded_string_view(*ps);
    mrb_cpp_new<padded_string_view>(
      mrb, self,
      std::move(psv)
    );
  }

  mrb_iv_set(mrb, self, MRB_SYM(buf), buf);

  return self;
}

static mrb_value
mrb_json_doc_initialize(mrb_state *mrb, mrb_value self)
{
  mrb_value view_obj;
  mrb_value parser_obj = mrb_undef_value();

  mrb_get_args(mrb, "o|o", &view_obj, &parser_obj);
  auto *view = mrb_cpp_get<padded_string_view>(mrb, view_obj);

  struct RClass *json_mod = mrb_module_get_id(mrb, MRB_SYM(JSON));
  if (mrb_undef_p(parser_obj)) {
    parser_obj = mrb_obj_new(
      mrb,
      mrb_class_get_under_id(mrb, json_mod, MRB_SYM(Parser)),
      0, NULL
    );
  }

  auto *parser = mrb_cpp_get<ondemand::parser>(mrb, parser_obj);
  auto *doc = mrb_cpp_new<ondemand::document>(mrb, self);
  auto code = parser->iterate(*view).get(*doc);
  if (likely(code == SUCCESS)) {
    // Store Ruby ivars for rehydration later
    mrb_iv_set(mrb, self, MRB_SYM(view), view_obj);
    mrb_iv_set(mrb, self, MRB_SYM(parser), parser_obj);

    return self;
  }
  raise_simdjson_error(mrb, code);
  return mrb_undef_value();
}

static mrb_value
mrb_json_parse_lazy(mrb_state *mrb, mrb_value self)
{
  mrb_value str;
  mrb_value parser_obj = mrb_undef_value();
  mrb_get_args(mrb, "S|o", &str, &parser_obj);

  struct RClass *json_mod = mrb_class_ptr(self);

  // 1. Build padded_string_view from Ruby string
  mrb_value view_obj = make_padded_string_view_from_ruby_str(mrb, str);

  // 2. Create Parser
  if (mrb_undef_p(parser_obj)) {
    parser_obj = mrb_obj_new(
      mrb,
      mrb_class_get_under_id(mrb, json_mod, MRB_SYM(Parser)),
      0, NULL
    );
  }

  // 3. Create Document(view, parser)
  mrb_value args[2] = { view_obj, parser_obj };
  return mrb_obj_new(
    mrb,
    mrb_class_get_under_id(mrb, json_mod, MRB_SYM(Document)),
    2, args
  );
}

static mrb_value
mrb_json_load_lazy(mrb_state *mrb, mrb_value self)
{
  mrb_value path;
  mrb_value parser_obj = mrb_undef_value();
  mrb_get_args(mrb, "S|o", &path, &parser_obj);

  struct RClass *json_mod = mrb_class_ptr(self);

  // 1. Load padded_string_view from file
  mrb_value view_obj = mrb_funcall_argv(
    mrb,
    mrb_obj_value(mrb_class_get_under_id(mrb, json_mod, MRB_SYM(PaddedString))),
    MRB_SYM(load),
    1,
    &path
  );

  // 2. Create Parser if none provided
  if (mrb_undef_p(parser_obj)) {
    parser_obj = mrb_obj_new(
      mrb,
      mrb_class_get_under_id(mrb, json_mod, MRB_SYM(Parser)),
      0, NULL
    );
  }

  // 3. Create Document(view, parser)
  mrb_value args[] = { view_obj, parser_obj };
  return mrb_obj_new(
    mrb,
    mrb_class_get_under_id(mrb, json_mod, MRB_SYM(Document)),
    2, args
  );
}

static mrb_value convert_ondemand_value_to_mrb(mrb_state* mrb, ondemand::value& v);

static mrb_value
convert_ondemand_array(mrb_state* mrb, ondemand::array arr)
{
  bool is_empty;
  auto code = arr.is_empty().get(is_empty);
  if (likely(code == SUCCESS)) {
    if (is_empty) {
      return mrb_ary_new(mrb);
    }

    mrb_value ary = mrb_ary_new(mrb);
    int arena = mrb_gc_arena_save(mrb);
    for (ondemand::value val : arr) {
      mrb_ary_push(mrb, ary, convert_ondemand_value_to_mrb(mrb, val));
      mrb_gc_arena_restore(mrb, arena);
    }
    return ary;
  }

  raise_simdjson_error(mrb, code);
  return mrb_undef_value();
}

static mrb_value
convert_ondemand_object(mrb_state* mrb, ondemand::object obj)
{
  bool is_empty;
  auto code = obj.is_empty().get(is_empty);
  if (likely(code == SUCCESS)) {
    if (is_empty) {
      return mrb_hash_new(mrb);
    }

    mrb_value hash = mrb_hash_new(mrb);
    int arena = mrb_gc_arena_save(mrb);
    for (auto field : obj) {
      std::string_view k;
      ondemand::value v;
      code = field.unescaped_key().get(k);
      if (likely(code == SUCCESS)) {
        code = field.value().get(v);
      }
      if (likely(code == SUCCESS)) {
        mrb_value key = mrb_str_new(mrb, k.data(), k.size());
        mrb_value val = convert_ondemand_value_to_mrb(mrb, v);
        mrb_hash_set(mrb, hash, key, val);
        mrb_gc_arena_restore(mrb, arena);
      } else {
        raise_simdjson_error(mrb, code);
      }
    }
    return hash;
  }

  raise_simdjson_error(mrb, code);
  return mrb_undef_value();
}

static mrb_value
convert_number_from_ondemand(mrb_state *mrb, ondemand::value& v)
{
  using namespace ondemand;

  number number;
  auto code = v.get_number().get(number);
  if (likely(code == SUCCESS)) {
    switch (number.get_number_type()) {
      case number_type::floating_point_number: {
        return mrb_convert_number(mrb, number.get_double());
      } break;
      case number_type::signed_integer: {
        return mrb_convert_number(mrb, number.get_int64());
      } break;

      case number_type::unsigned_integer: {
        return mrb_convert_number(mrb, number.get_uint64());
      } break;

      case number_type::big_integer: {
        auto sv = v.raw_json_token();

        return mrb_str_to_integer(mrb, mrb_str_new_static(mrb, sv.data(), sv.size()), 0, 0);
      } break;

      default:
        mrb_raise(mrb, E_JSON_NUMBER_ERROR, "unknown number type");
    }
  }

  raise_simdjson_error(mrb, code);
  return mrb_undef_value();
}

static mrb_value
convert_string_from_ondemand(mrb_state* mrb, ondemand::value& v)
{
  std::string_view dec;
  auto code = v.get_string().get(dec);

  if (likely(code == SUCCESS)) {
    return mrb_str_new(mrb, dec.data(), dec.size());
  }

  raise_simdjson_error(mrb, code);
  return mrb_undef_value();
}

static mrb_value
convert_ondemand_value_to_mrb(mrb_state* mrb, ondemand::value& v)
{
  using namespace ondemand;
  switch (v.type()) {
    case json_type::object:
      return convert_ondemand_object(mrb, v.get_object());
    case json_type::array:
      return convert_ondemand_array(mrb, v.get_array());
    case json_type::string:
      return convert_string_from_ondemand(mrb, v);
    case json_type::number:
      return convert_number_from_ondemand(mrb, v);
    case json_type::boolean:
      return mrb_bool_value(v.get_bool());
    case json_type::null:
      return mrb_nil_value();
    case json_type::unknown:
    default:
       mrb_raise(mrb, E_TYPE_ERROR, "unknown JSON type");
       break;
  }
  return mrb_undef_value();

}

static ondemand::document*
mrb_json_doc_get(mrb_state* mrb, mrb_value self)
{
  ondemand::document *doc = mrb_cpp_get<ondemand::document>(mrb, self);

  if (likely(doc->is_alive())) {
    return doc;
  }

  mrb_value view_obj   = mrb_iv_get(mrb, self, MRB_SYM(view));
  mrb_value parser_obj = mrb_iv_get(mrb, self, MRB_SYM(parser));

  auto *view = mrb_cpp_get<padded_string_view>(mrb, view_obj);
  auto *parser = mrb_cpp_get<ondemand::parser>(mrb, parser_obj);

  auto code = parser->iterate(*view).get(*doc);
  if (likely(code == SUCCESS)) {

    return doc;
  }

  raise_simdjson_error(mrb, code);
  return nullptr;
}

static bool
is_lookup_miss(simdjson::error_code code)
{
  return code == NO_SUCH_FIELD ||
         code == OUT_OF_BOUNDS ||
         code == INDEX_OUT_OF_BOUNDS ||
         code == INCORRECT_TYPE;
}

static mrb_value
mrb_json_doc_aref(mrb_state* mrb, mrb_value self)
{
  mrb_value key;
  mrb_get_args(mrb, "S", &key);

  auto *const doc = mrb_json_doc_get(mrb, self);

  std::string_view k(RSTRING_PTR(key), RSTRING_LEN(key));
  ondemand::value val;
  auto code = (*doc)[k].get(val);
  if (likely(code == SUCCESS)) return convert_ondemand_value_to_mrb(mrb, val);

  if (is_lookup_miss(code))  return mrb_nil_value();

  raise_simdjson_error(mrb, code);
  return mrb_undef_value();
}

static mrb_value
mrb_json_doc_fetch(mrb_state* mrb, mrb_value self)
{
  mrb_value key_or_index;
  mrb_value default_val = mrb_undef_value();
  mrb_value block = mrb_undef_value();

  // accept any object as first arg, optional default, optional block
  mrb_get_args(mrb, "o|o&", &key_or_index, &default_val, &block);

  auto *const doc = mrb_json_doc_get(mrb, self);

  // If first arg is an Integer -> array index path
  if (mrb_integer_p(key_or_index)) {
    mrb_int idx = mrb_integer(key_or_index);
    ondemand::value val;
    auto code = doc->at(static_cast<size_t>(idx)).get(val);

    if (likely(code == SUCCESS)) {
      return convert_ondemand_value_to_mrb(mrb, val);
    }

    if (is_lookup_miss(code)) {
      if (!mrb_undef_p(default_val)) return default_val;
      if (mrb_proc_p(block)) return mrb_yield(mrb, block, key_or_index);
      mrb_raise(mrb, E_INDEX_ERROR, "index not found");
    }

    raise_simdjson_error(mrb, code);
    return mrb_undef_value(); // unreachable
  }

  // Otherwise treat as string key (convert to string view)
  mrb_value key_str = mrb_obj_as_string(mrb, key_or_index);
  std::string_view k(RSTRING_PTR(key_str), RSTRING_LEN(key_str));
  ondemand::value val;
  auto code = (*doc)[k].get(val);

  if (likely(code == SUCCESS)) {
    return convert_ondemand_value_to_mrb(mrb, val);
  }

  if (is_lookup_miss(code)) {
    if (!mrb_undef_p(default_val)) return default_val;
    if (mrb_proc_p(block)) return mrb_yield(mrb, block, key_or_index);
    mrb_raise(mrb, E_KEY_ERROR, "key not found");
  }

  raise_simdjson_error(mrb, code);
  return mrb_undef_value(); // unreachable
}

static mrb_value
mrb_json_doc_find_field(mrb_state* mrb, mrb_value self)
{
  mrb_value key;
  mrb_get_args(mrb, "S", &key);

  auto *const doc = mrb_json_doc_get(mrb, self);

  std::string_view k(RSTRING_PTR(key), RSTRING_LEN(key));
  ondemand::value val;
  const auto code = doc->find_field(k).get(val);
  if (likely(code == SUCCESS)) return convert_ondemand_value_to_mrb(mrb, val);

  if (is_lookup_miss(code))  return mrb_nil_value();

  raise_simdjson_error(mrb, code);
  return mrb_undef_value(); // unreachable
}

static mrb_value
mrb_json_doc_find_field_unordered(mrb_state* mrb, mrb_value self)
{
  mrb_value key;
  mrb_get_args(mrb, "S", &key);

  auto *const doc = mrb_json_doc_get(mrb, self);

  std::string_view k(RSTRING_PTR(key), RSTRING_LEN(key));
  ondemand::value val;
  auto code = doc->find_field_unordered(k).get(val);
  if (likely(code == SUCCESS)) return convert_ondemand_value_to_mrb(mrb, val);
  if (is_lookup_miss(code))  return mrb_nil_value();

  raise_simdjson_error(mrb, code);
  return mrb_undef_value(); // unreachable
}

static mrb_value
mrb_json_doc_at(mrb_state* mrb, mrb_value self)
{
  mrb_int index;
  mrb_get_args(mrb, "i", &index);

  auto *const doc = mrb_json_doc_get(mrb, self);

  ondemand::value val;
  const auto code = doc->at(index).get(val);
  if (likely(code == SUCCESS)) {
    return convert_ondemand_value_to_mrb(mrb, val);
  }

  if (is_lookup_miss(code))  return mrb_nil_value();

  raise_simdjson_error(mrb, code);
  return mrb_undef_value(); // unreachable
}

static mrb_value
mrb_json_doc_at_pointer(mrb_state* mrb, mrb_value self)
{
  mrb_value ptr_val;
  mrb_get_args(mrb, "S", &ptr_val);

  auto *const doc = mrb_json_doc_get(mrb, self);

  std::string_view json_pointer(RSTRING_PTR(ptr_val), RSTRING_LEN(ptr_val));
  ondemand::value value;
  auto code = doc->at_pointer(json_pointer).get(value);
  if (likely(code == SUCCESS)) {
    return convert_ondemand_value_to_mrb(mrb, value);
  }

  if (is_lookup_miss(code))  return mrb_nil_value();

  raise_simdjson_error(mrb, code);
  return mrb_undef_value(); // unreachable
}

static mrb_value
mrb_json_doc_at_path(mrb_state* mrb, mrb_value self)
{
  mrb_value path_val;
  mrb_get_args(mrb, "S", &path_val);

  auto *const doc = mrb_json_doc_get(mrb, self);

  std::string_view json_path(RSTRING_PTR(path_val), RSTRING_LEN(path_val));
  ondemand::value value;
  auto code = doc->at_path(json_path).get(value);
  if (likely(code == SUCCESS)) {
    return convert_ondemand_value_to_mrb(mrb, value);
  }

  if (is_lookup_miss(code))  return mrb_nil_value();

  raise_simdjson_error(mrb, code);
  return mrb_undef_value();
}

static mrb_value
mrb_json_doc_at_path_with_wildcard(mrb_state* mrb, mrb_value self)
{
  mrb_value path_val;
  mrb_value block = mrb_undef_value();
  mrb_get_args(mrb, "S|&", &path_val, &block);

  auto *const doc = mrb_json_doc_get(mrb, self);

  std::string_view json_path(RSTRING_PTR(path_val), RSTRING_LEN(path_val));
  std::vector<ondemand::value> values;
  auto code = doc->at_path_with_wildcard(json_path).get(values);
  if (likely(code == SUCCESS)) {
    if (mrb_proc_p(block)) {
      int arena = mrb_gc_arena_save(mrb);
      for (auto v : values) {
        mrb_yield(mrb, block, convert_ondemand_value_to_mrb(mrb, v));
        mrb_gc_arena_restore(mrb, arena);
      }
      return self;
    } else {
      mrb_value ary = mrb_ary_new_capa(mrb, values.size());
      int arena = mrb_gc_arena_save(mrb);
      for (auto v : values) {
        mrb_ary_push(mrb, ary, convert_ondemand_value_to_mrb(mrb, v));
        mrb_gc_arena_restore(mrb, arena);
      }
      return ary;
    }
  }
  if (is_lookup_miss(code))  return mrb_nil_value();

  raise_simdjson_error(mrb, code);
  return mrb_undef_value();
}

static mrb_value
mrb_json_doc_array_each(mrb_state* mrb, mrb_value self)
{
  mrb_value block = mrb_undef_value();
  mrb_get_args(mrb, "|&", &block);

  auto *const doc = mrb_json_doc_get(mrb, self);
  ondemand::array array;
  auto code = doc->get_array().get(array);
  if (likely(code == SUCCESS)) {
    if (mrb_proc_p(block)) {
      int arena = mrb_gc_arena_save(mrb);
      for (ondemand::value v : array) {
        mrb_value ruby_val = convert_ondemand_value_to_mrb(mrb, v);
        mrb_yield(mrb, block, ruby_val);
        mrb_gc_arena_restore(mrb, arena);
      }
      return self;
    } else {
      size_t capa;
      code = array.count_elements().get(capa);
      if (likely(code == SUCCESS)) {
        mrb_value ary = mrb_ary_new_capa(mrb, capa);
        int arena = mrb_gc_arena_save(mrb);
        for (ondemand::value v : array) {
          mrb_value ruby_val = convert_ondemand_value_to_mrb(mrb, v);
          mrb_ary_push(mrb, ary, ruby_val);
          mrb_gc_arena_restore(mrb, arena);
        }
        return ary;
      }
      raise_simdjson_error(mrb, code);
    }
  }

  raise_simdjson_error(mrb, code);
  return mrb_undef_value();
}

static mrb_value
mrb_json_doc_object_each(mrb_state* mrb, mrb_value self)
{
  mrb_value block = mrb_undef_value();
  mrb_get_args(mrb, "|&", &block);

  auto* const doc = mrb_json_doc_get(mrb, self);
  ondemand::object object;
  auto code = doc->get_object().get(object);
  if (likely(code == SUCCESS)) {
    if (mrb_proc_p(block)) {
      int arena = mrb_gc_arena_save(mrb);
      for (auto field : object) {
        std::string_view k;
        ondemand::value v;
        code = field.unescaped_key().get(k);
        if (likely(code == SUCCESS)) {
          code = field.value().get(v);
        }
        if (likely(code == SUCCESS)) {

          mrb_value key = mrb_str_new(mrb, k.data(), k.size());
          mrb_value val = convert_ondemand_value_to_mrb(mrb, v);

          mrb_value argv[] = {key, val};
          mrb_yield_argv(mrb, block, 2, argv);

          mrb_gc_arena_restore(mrb, arena);
        } else {
          raise_simdjson_error(mrb, code);
        }
      }

      return self;
    } else {
      mrb_value hash = mrb_hash_new(mrb);
      int arena = mrb_gc_arena_save(mrb);

      for (auto field : object) {
        std::string_view k;
        ondemand::value v;
        code = field.unescaped_key().get(k);
        if (likely(code == SUCCESS)) {
          code = field.value().get(v);
        }
        if (likely(code == SUCCESS)) {

          mrb_value key = mrb_str_new(mrb, k.data(), k.size());
          mrb_value val = convert_ondemand_value_to_mrb(mrb, v);

          mrb_hash_set(mrb, hash, key, val);

          mrb_gc_arena_restore(mrb, arena);
        }
      }
      return hash;
    }
  }

  raise_simdjson_error(mrb, code);
  return mrb_undef_value();
}

static mrb_value
mrb_json_doc_rewind(mrb_state* mrb, mrb_value self)
{
  auto *const doc = mrb_cpp_get<ondemand::document>(mrb, self);

  doc->rewind();

  return self;
}

static mrb_value
mrb_json_doc_reiterate(mrb_state *mrb, mrb_value self)
{
  mrb_value view_obj = mrb_iv_get(mrb, self, MRB_SYM(view));
  mrb_value parser_obj = mrb_iv_get(mrb, self, MRB_SYM(parser));

  auto *view   = mrb_cpp_get<padded_string_view>(mrb, view_obj);
  auto *parser = mrb_cpp_get<ondemand::parser>(mrb, parser_obj);

  auto result = parser->iterate(*view);
  if (likely(result.error() == SUCCESS)) {
    auto *const doc_cpp = mrb_cpp_get<ondemand::document>(mrb, self);
    *doc_cpp = std::move(result.value());

    return self;
  }

  raise_simdjson_error(mrb, result.error());
  return mrb_undef_value();
}

static inline void json_encode_nil(builder::string_builder &builder) {
  builder.append_null();
}

static inline void json_encode_false(builder::string_builder &builder) {
  builder.append(false);
}

static inline void json_encode_false_type(mrb_value v,
                                          builder::string_builder &builder) {
  if (mrb_nil_p(v)) {
    json_encode_nil(builder);
  } else {
    json_encode_false(builder);
  }
}

static inline void json_encode_true(builder::string_builder &builder) {
  builder.append(true);
}

static inline void json_encode_string(mrb_value v, builder::string_builder &builder) {
  std::string_view sv(RSTRING_PTR(v), RSTRING_LEN(v));
  builder.escape_and_append_with_quotes(sv);
}

static inline void json_encode_symbol(mrb_state *mrb, mrb_value v,
                               builder::string_builder &builder) {
  json_encode_string(mrb_sym_str(mrb, mrb_symbol(v)), builder);
}
#ifndef MRB_NO_FLOAT
static inline void json_encode_float(mrb_value v,
                                     builder::string_builder &builder) {
  builder.append(mrb_float(v));
}
#endif
static inline void json_encode_integer(mrb_value v,
                                       builder::string_builder &builder) {
  builder.append(mrb_integer(v));
}

struct DumpHashCtx {
  builder::string_builder &builder;
  bool first;
};

static void json_encode(mrb_state *mrb, mrb_value v,
                        builder::string_builder &builder);

static int dump_hash_cb(mrb_state *mrb, mrb_value key, mrb_value val,
                        void * const data) {
  auto * const ctx = static_cast<DumpHashCtx *>(data);

  if (ctx->first)
    ctx->first = false;
  else
    ctx->builder.append_comma();

  json_encode(mrb, mrb_obj_as_string(mrb, key), ctx->builder);
  ctx->builder.append_colon();
  json_encode(mrb, val, ctx->builder);

  return 0; // continue iteration
}

static void json_encode_hash(mrb_state *mrb, mrb_value v,
                             builder::string_builder &builder) {
  builder.start_object();
  DumpHashCtx ctx{builder, true};
  mrb_hash_foreach(mrb, mrb_hash_ptr(v), dump_hash_cb, &ctx);
  builder.end_object();
}

static void json_encode_array(mrb_state *mrb, mrb_value v,
                              builder::string_builder &builder) {
  builder.start_array();
  const mrb_int n = RARRAY_LEN(v);

  if (n > 0) {
    json_encode(mrb, mrb_ary_ref(mrb, v, 0), builder);

    for (mrb_int i = 1; i < n; ++i) {
      builder.append_comma();
      json_encode(mrb, mrb_ary_ref(mrb, v, i), builder);
    }
  }

  builder.end_array();
}

static void json_encode(mrb_state *mrb, mrb_value v,
                        builder::string_builder &builder) {
  switch (mrb_type(v)) {
    case MRB_TT_FALSE: {
      json_encode_false_type(v, builder);
    } break;
    case MRB_TT_TRUE: {
      json_encode_true(builder);
    } break;
    case MRB_TT_SYMBOL: {
      json_encode_symbol(mrb, v, builder);
    } break;
  #ifndef MRB_NO_FLOAT
    case MRB_TT_FLOAT: {
      json_encode_float(v, builder);
    } break;
  #endif
    case MRB_TT_INTEGER: {
      json_encode_integer(v, builder);
    } break;
    case MRB_TT_HASH: {
      json_encode_hash(mrb, v, builder);
    } break;
    case MRB_TT_ARRAY: {
      json_encode_array(mrb, v, builder);
    } break;
    case MRB_TT_STRING: {
      json_encode_string(v, builder);
    } break;
    default: {
      json_encode_string(mrb_obj_as_string(mrb, v), builder);
    }
  }
}

MRB_API mrb_value mrb_json_dump(mrb_state *mrb, mrb_value obj) {
  builder::string_builder sb;
  json_encode(mrb, obj, sb);
  if (likely(sb.validate_unicode())) {
    std::string_view sv = sb.view();
    return mrb_str_new(mrb, sv.data(), sv.size());
  }

   mrb_raise(mrb, E_JSON_UTF8_ERROR, "invalid utf-8");
   return mrb_undef_value();
}

static mrb_value mrb_json_dump_m(mrb_state *mrb, mrb_value self) {
  mrb_value obj;
  mrb_get_args(mrb, "o", &obj);

  return mrb_json_dump(mrb, obj);
}

#define DEFINE_MRB_TO_JSON(func_name, ENCODER_CALL)                            \
  static mrb_value func_name(mrb_state *mrb, mrb_value o) {                    \
    builder::string_builder sb;                                                \
    ENCODER_CALL;                                                              \
    if (unlikely(!sb.validate_unicode())) {                                    \
      mrb_raise(mrb, E_JSON_UTF8_ERROR, "invalid utf-8");                      \
    }                                                                          \
    std::string_view sv = sb.view();                                           \
    return mrb_str_new(mrb, sv.data(), sv.size());                             \
  }

DEFINE_MRB_TO_JSON(mrb_string_to_json, json_encode_string(o, sb));
DEFINE_MRB_TO_JSON(mrb_array_to_json, json_encode_array(mrb, o, sb));
DEFINE_MRB_TO_JSON(mrb_hash_to_json, json_encode_hash(mrb, o, sb));
#ifndef MRB_NO_FLOAT
DEFINE_MRB_TO_JSON(mrb_float_to_json, json_encode_float(o, sb));
#endif
DEFINE_MRB_TO_JSON(mrb_integer_to_json, json_encode_integer(o, sb));
DEFINE_MRB_TO_JSON(mrb_true_to_json, json_encode_true(sb));
DEFINE_MRB_TO_JSON(mrb_false_to_json, json_encode_false(sb));
DEFINE_MRB_TO_JSON(mrb_nil_to_json, json_encode_nil(sb));
DEFINE_MRB_TO_JSON(mrb_symbol_to_json, json_encode_symbol(mrb, o, sb));

MRB_API mrb_value
mrb_json_load(mrb_state *mrb, mrb_value path_str, mrb_bool symbolize_names)
{
  std::string_view path(RSTRING_PTR(path_str), RSTRING_LEN(path_str));
  auto res = padded_string::load(path);
  if (unlikely(res.error() != SUCCESS)) {
    mrb_sys_fail(mrb, "failed to read file");
  }

  dom::parser parser;
  auto result = parser.parse(res.value());

  if (unlikely(result.error() != SUCCESS)) {
    raise_simdjson_error(mrb, result.error());
  }

  return convert_element(mrb, result.value(), symbolize_names);
}

static mrb_value
mrb_json_load_m(mrb_state *mrb, mrb_value self)
{
  mrb_value path_str;
  mrb_value kw_values[1] = {
      mrb_undef_value()}; // default value for symbolize_names
  mrb_sym kw_names[] = {MRB_SYM(symbolize_names)};
  mrb_kwargs kwargs = {1, // num: number of keywords
                       0, // required: none required
                       kw_names, kw_values, NULL};

  mrb_get_args(mrb, "S:", &path_str, &kwargs);

  // Fallback default
  mrb_bool symbolize_names = FALSE;
  if (!mrb_undef_p(kw_values[0])) {
    symbolize_names = mrb_bool(kw_values[0]); // cast to mrb_bool
  }

  return mrb_json_load(mrb, path_str, symbolize_names);
}

MRB_BEGIN_DECL
void mrb_mruby_fast_json_gem_init(mrb_state *mrb) {

#ifdef _WIN32
  SYSTEM_INFO sysInfo;
  GetSystemInfo(&sysInfo);
  pagesize = sysInfo.dwPageSize;
#else
  pagesize = sysconf(_SC_PAGESIZE);
#endif
  if (pagesize < 1) {
    mrb_bug(mrb, "got non positive pagesize");
  }

  struct RClass *json_mod = mrb_define_module_id(mrb, MRB_SYM(JSON));
  auto impl = simdjson::get_active_implementation()->description();
  mrb_value impl_name = mrb_str_new(mrb, impl.data(), impl.size());
  mrb_define_const_id(mrb, json_mod, MRB_SYM(SIMD_IMPLEMENTATION),  impl_name);
  struct RClass *json_error = mrb_define_class_under_id(
      mrb, json_mod, MRB_SYM(ParserError), mrb->eStandardError_class);

#define DEFINE_JSON_ERROR(NAME)                                                \
  mrb_define_class_under_id(mrb, json_mod, MRB_SYM(NAME##Error), json_error)

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

  DEFINE_JSON_ERROR(TAtom);
  DEFINE_JSON_ERROR(FAtom);
  DEFINE_JSON_ERROR(NAtom);

  DEFINE_JSON_ERROR(BigInt);
  DEFINE_JSON_ERROR(NumberOutOfRange);

  DEFINE_JSON_ERROR(UnescapedChars);

  DEFINE_JSON_ERROR(Uninitialized);
  DEFINE_JSON_ERROR(ParserInUse);
  DEFINE_JSON_ERROR(ScalarDocumentAsValue);

  DEFINE_JSON_ERROR(IncompleteArrayOrObject);
  DEFINE_JSON_ERROR(TrailingContent);

  DEFINE_JSON_ERROR(OutOfCapacity);
  DEFINE_JSON_ERROR(InsufficientPadding);

  DEFINE_JSON_ERROR(IndexOutOfBounds);
  DEFINE_JSON_ERROR(OutOfBounds);
  DEFINE_JSON_ERROR(OutOfOrderIteration);
  DEFINE_JSON_ERROR(NoSuchField);

  DEFINE_JSON_ERROR(IO);
  DEFINE_JSON_ERROR(InvalidJSONPointer);
  DEFINE_JSON_ERROR(InvalidURIFragment);

  DEFINE_JSON_ERROR(UnsupportedArchitecture);
  DEFINE_JSON_ERROR(Unexpected);

  mrb_define_module_function_id(mrb, json_mod, MRB_SYM(parse), mrb_json_parse_m,
                             MRB_ARGS_REQ(1) | MRB_ARGS_KEY(1, 0));
  mrb_define_module_function_id(mrb, json_mod, MRB_SYM(dump), mrb_json_dump_m,
                             MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, json_mod, MRB_SYM(parse_lazy), mrb_json_parse_lazy,
                             MRB_ARGS_ARG(1, 1));
    mrb_define_module_function_id(mrb, json_mod, MRB_SYM(load_lazy), mrb_json_load_lazy,
                             MRB_ARGS_ARG(1, 1));
  mrb_define_module_function_id(mrb, json_mod, MRB_SYM(load), mrb_json_load_m,
                             MRB_ARGS_REQ(1) | MRB_ARGS_KEY(1, 0));

  mrb_define_method_id(mrb, mrb->object_class, MRB_SYM(to_json), mrb_json_dump,
                       MRB_ARGS_NONE());
  mrb_define_method_id(mrb, mrb->string_class, MRB_SYM(to_json),
                       mrb_string_to_json, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, mrb->array_class, MRB_SYM(to_json),
                       mrb_array_to_json, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, mrb->hash_class, MRB_SYM(to_json), mrb_hash_to_json,
                       MRB_ARGS_NONE());
#ifndef MRB_NO_FLOAT
  mrb_define_method_id(mrb, mrb->float_class, MRB_SYM(to_json),
                       mrb_float_to_json, MRB_ARGS_NONE());
#endif
  mrb_define_method_id(mrb, mrb->integer_class, MRB_SYM(to_json),
                       mrb_integer_to_json, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, mrb->true_class, MRB_SYM(to_json), mrb_true_to_json,
                       MRB_ARGS_NONE());
  mrb_define_method_id(mrb, mrb->false_class, MRB_SYM(to_json),
                       mrb_false_to_json, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, mrb->nil_class, MRB_SYM(to_json), mrb_nil_to_json,
                       MRB_ARGS_NONE());
  mrb_define_method_id(mrb, mrb->symbol_class, MRB_SYM(to_json),
                       mrb_symbol_to_json, MRB_ARGS_NONE());

  //
  // JSON::Parser
  //
  struct RClass *parser_cls =
    mrb_define_class_under_id(mrb, json_mod, MRB_SYM(Parser), mrb->object_class);
  MRB_SET_INSTANCE_TT(parser_cls, MRB_TT_CDATA);

  mrb_define_method_id(mrb, parser_cls, MRB_SYM(initialize),
                       mrb_ondemand_parser_initialize, MRB_ARGS_OPT(1));
  mrb_define_method_id(mrb, parser_cls, MRB_SYM(allocate),
                       mrb_ondemand_parser_allocate, MRB_ARGS_OPT(1));
  mrb_define_method_id(mrb, parser_cls, MRB_SYM(iterate),
                       mrb_ondemand_parser_iterate, MRB_ARGS_REQ(1));

  //
  // JSON::PaddedString
  //
  struct RClass *ps_cls =
    mrb_define_class_under_id(mrb, json_mod, MRB_SYM(PaddedString), mrb->object_class);
  MRB_SET_INSTANCE_TT(ps_cls, MRB_TT_CDATA);

  mrb_define_method_id(mrb, ps_cls, MRB_SYM(initialize),
                       mrb_padded_string_initialize, MRB_ARGS_REQ(1));
  mrb_define_class_method_id(mrb, ps_cls, MRB_SYM(load), mrb_padded_string_s_load, MRB_ARGS_REQ(1));

  //
  // JSON::PaddedStringView
  //
  struct RClass *psv_cls =
    mrb_define_class_under_id(mrb, json_mod, MRB_SYM(PaddedStringView), mrb->object_class);
  MRB_SET_INSTANCE_TT(psv_cls, MRB_TT_CDATA);

  mrb_define_method_id(mrb, psv_cls, MRB_SYM(initialize),
                       mrb_padded_string_view_initialize, MRB_ARGS_ARG(0, 2));

  struct RClass* doc_cls =
    mrb_define_class_under_id(mrb, json_mod, MRB_SYM(Document), mrb->object_class);
  MRB_SET_INSTANCE_TT(doc_cls, MRB_TT_CDATA);

  mrb_define_method_id(mrb, doc_cls, MRB_SYM(initialize),
                      mrb_json_doc_initialize, MRB_ARGS_ARG(1, 1));
  mrb_define_method_id(mrb, doc_cls, MRB_OPSYM(aref),
                      mrb_json_doc_aref, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, doc_cls, MRB_SYM(find_field),
                      mrb_json_doc_find_field, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, doc_cls, MRB_SYM(fetch),
                      mrb_json_doc_fetch, MRB_ARGS_ARG(1, 1) | MRB_ARGS_BLOCK());

  mrb_define_method_id(mrb, doc_cls, MRB_SYM(find_field_unordered),
                      mrb_json_doc_find_field_unordered, MRB_ARGS_REQ(1));
  mrb_define_method_id( mrb, doc_cls, MRB_SYM(at),
                      mrb_json_doc_at, MRB_ARGS_REQ(1));
  mrb_define_method_id( mrb, doc_cls, MRB_SYM(at_pointer),
                      mrb_json_doc_at_pointer, MRB_ARGS_REQ(1));

  mrb_define_method_id(mrb, doc_cls, MRB_SYM(at_path),
                      mrb_json_doc_at_path, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, doc_cls, MRB_SYM(at_path_with_wildcard),
                      mrb_json_doc_at_path_with_wildcard, MRB_ARGS_REQ(1)|MRB_ARGS_BLOCK());
  mrb_define_method_id(mrb, doc_cls, MRB_SYM(rewind),
                     mrb_json_doc_rewind, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, doc_cls, MRB_SYM(reiterate),
                     mrb_json_doc_reiterate, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, doc_cls, MRB_SYM(array_each),
                      mrb_json_doc_array_each, MRB_ARGS_BLOCK());
    mrb_define_method_id(mrb, doc_cls, MRB_SYM(object_each),
                      mrb_json_doc_object_each, MRB_ARGS_BLOCK());
}

void mrb_mruby_fast_json_gem_final(mrb_state *mrb) {}
MRB_END_DECL
