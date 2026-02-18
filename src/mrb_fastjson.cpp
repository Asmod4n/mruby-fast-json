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
#include <simdjson.h>
#include <mruby/variable.h>
#include <string_view>
MRB_BEGIN_DECL
#include <mruby/internal.h>
MRB_END_DECL
using namespace simdjson;

#ifdef _WIN32
#include <windows.h>
#include <sysinfoapi.h>
#else
#include <unistd.h>
#endif
#include <cstdio>


// Returns the default size of the page in bytes on this system.
static long pagesize;

// Returns true if the buffer + len + simdjson::SIMDJSON_PADDING crosses the
// page boundary.
bool need_allocation(const char* buf, size_t len, size_t capa) {
#ifdef MRB_DEBUG
  return true; // always allocate padded_string in debug mode to detect issues
#endif
  // 2. Check Ruby's reported capacity
  if (capa >= len + SIMDJSON_PADDING) {
    return false; // safe
  }

  // 3. Page boundary fallback (always safe)
  uintptr_t end = reinterpret_cast<uintptr_t>(buf + len - 1);
  uintptr_t offset = end % pagesize;
  if (offset + SIMDJSON_PADDING < static_cast<uintptr_t>(pagesize)) {
      return false;
  }

  return true; // must allocate padded_string
}

static padded_string_view
simdjson_safe_view_from_mrb_string(mrb_state *mrb, mrb_value str,
                                   padded_string &jsonbuffer) {
  size_t len = RSTRING_LEN(str);
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

  size_t required = len + SIMDJSON_PADDING;

  if ((size_t)RSTRING_CAPA(str) < required) {
    // grow Ruby string to required bytes (len + padding)
    str = mrb_str_resize(mrb, str, required);
    str = mrb_obj_freeze(mrb, str);
    // restore logical length to original JSON length
    RSTR_SET_LEN(RSTRING(str), len);
  }

  // capacity is now at least `required`
  return padded_string_view(RSTRING_PTR(str), len, required);
}

static mrb_value convert_array(mrb_state *mrb, dom::element arr_el,
                               mrb_bool symbolize_names);
static mrb_value convert_object(mrb_state *mrb, dom::element obj_el,
                                mrb_bool symbolize_names);

static mrb_value convert_element(mrb_state *mrb, dom::element el,
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

static mrb_value convert_array(mrb_state *mrb, dom::element arr_el,
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

static mrb_value convert_object(mrb_state *mrb, dom::element obj_el,
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

static void raise_simdjson_error(mrb_state *mrb, error_code code) {
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

  case SUCCESS:
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

// ondemand API implementation
struct mrb_json_doc {
  ondemand::parser parser;
  padded_string jsonbuffer;
  padded_string_view buffer;
  ondemand::document doc;
  mrb_value source;
  bool need_to_reparse = false;

  mrb_json_doc(mrb_state* mrb, mrb_value self, mrb_value str) {
    buffer = simdjson_safe_view_from_mrb_string(mrb, str, jsonbuffer);
    mrb_iv_set(mrb, self, MRB_SYM(source), str);
    source = str;
  }
};
MRB_CPP_DEFINE_TYPE(mrb_json_doc, mrb_json_doc)

static void
raise_simdjson_error_with_reparse(mrb_state* mrb, mrb_json_doc* doc, error_code code)
{
  doc->need_to_reparse = true;
  raise_simdjson_error(mrb, code);
}

static mrb_value
mrb_json_doc_initialize(mrb_state* mrb, mrb_value self)
{
  mrb_value str;
  mrb_get_args(mrb, "S", &str);

  auto* doc = mrb_cpp_new<mrb_json_doc>(mrb, self, mrb, self, str);

  auto result = doc->parser.iterate(doc->buffer);
  if (unlikely(result.error())) raise_simdjson_error_with_reparse(mrb, doc, result.error());
  doc->doc = std::move(result.value());

  return self;
}

static mrb_value convert_ondemand_value_to_mrb(mrb_state* mrb, mrb_json_doc *doc, ondemand::value& v);

static mrb_value
convert_ondemand_array(mrb_state* mrb, mrb_json_doc *doc, ondemand::array arr)
{
  mrb_value ary = mrb_ary_new(mrb);
  if (arr.is_empty()) {
    return ary;
  }
  int arena = mrb_gc_arena_save(mrb);
  for (ondemand::value val : arr) {
    mrb_ary_push(mrb, ary, convert_ondemand_value_to_mrb(mrb, doc, val));
    mrb_gc_arena_restore(mrb, arena);
  }
  return ary;
}

static mrb_value
convert_ondemand_object(mrb_state* mrb, mrb_json_doc *doc, ondemand::object obj)
{
  mrb_value hash = mrb_hash_new(mrb);
  if (obj.is_empty()) {
    return hash;
  }
  int arena = mrb_gc_arena_save(mrb);
  for (auto field : obj) {
    std::string_view k = field.unescaped_key();
    ondemand::value v = field.value();
    mrb_value key = mrb_str_new(mrb, k.data(), k.size());
    mrb_value val = convert_ondemand_value_to_mrb(mrb, doc, v);
    mrb_hash_set(mrb, hash, key, val);
    mrb_gc_arena_restore(mrb, arena);
  }
  return hash;
}

#if defined(__SIZEOF_INT128__)
static bool parse_decimal_to_u128(std::string_view digits, unsigned __int128 &out) {
  const size_t len = digits.size();
  if (len == 0) return false;

  // quick length-based overflow rejection:
  // max decimal digits for unsigned 128 is 39 (2^128-1 has 39 digits)
  if (len > 39) return false;

  // general path: use unsigned __int128 with precomputed limit
  const unsigned __int128 U128_MAX = static_cast<unsigned __int128>(-1);
  const unsigned __int128 LIMIT = U128_MAX / 10;
  const unsigned int LIMIT_DIGIT = static_cast<unsigned int>(U128_MAX % 10);

  unsigned __int128 acc = 0;
  const char* p = digits.data();
  const char* end = p + len;
  while (p < end) {
    unsigned int d = static_cast<unsigned int>(*p++ - '0');
    if (d > 9u) return false;
    // check overflow: acc*10 + d > U128_MAX ?
    if (acc > LIMIT || (acc == LIMIT && d > LIMIT_DIGIT)) return false;
    acc = acc * 10 + d;
  }
  out = acc;
  return true;
}

static mrb_value
convert_big_integer_from_ondemand(mrb_state *mrb, mrb_json_doc *doc, ondemand::value& v)
{
  // sign (cheap)
  auto negative = v.is_negative();

  // get raw token text (no allocation)
  auto raw_res = v.raw_json();
  if (unlikely(raw_res.error())) raise_simdjson_error_with_reparse(mrb, doc, raw_res.error());
  std::string_view sv = raw_res.value();

  // strip sign if present in token text; rely on is_negative() for sign
  size_t pos = 0;
  if (!sv.empty() && (sv[0] == '+' || sv[0] == '-')) pos = 1;
  std::string_view digits = sv.substr(pos);

  if (unlikely(digits.empty())) {
    mrb_raise(mrb, E_TYPE_ERROR, "invalid big integer");
  }

  // parse into unsigned accumulator (fast)
  unsigned __int128 acc = 0;
  if (!parse_decimal_to_u128(digits, acc)) {
    // overflow beyond unsigned 128 or invalid digits — policy: return raw token
    return mrb_str_new(mrb, sv.data(), sv.size());
  }

  if (negative) {
    // signed 128 range: magnitude must be <= 2^127
    const unsigned __int128 SIGNED_LIMIT = (static_cast<unsigned __int128>(1) << 127);

    if (acc > SIGNED_LIMIT) {
      // too large to fit signed 128
      return mrb_str_new(mrb, sv.data(), sv.size());
    }

    // produce -2^127 safely without UB
    if (acc == SIGNED_LIMIT) {
      unsigned __int128 u = SIGNED_LIMIT;
      __int128 sval;
      std::memcpy(&sval, &u, sizeof(sval)); // bitwise copy: yields -2^127 on two's complement
      return mrb_convert_number(mrb, sval);
    } else {
      // acc < 2^127: safe to cast and negate
      __int128 sval = - static_cast<__int128>(acc);
      return mrb_convert_number(mrb, sval);
    }
  } else {
    // positive: return unsigned 128
    return mrb_convert_number(mrb, static_cast<unsigned __int128>(acc));
  }
}
#endif // __SIZEOF_INT128__

static mrb_value
convert_number_from_ondemand(mrb_state *mrb, mrb_json_doc *doc, ondemand::value& v)
{
  using namespace ondemand;

  auto nt_res = v.get_number_type();
  if (unlikely(nt_res.error())) raise_simdjson_error_with_reparse(mrb, doc, nt_res.error());

  switch (nt_res.value()) {
    case number_type::unsigned_integer: {
      auto ures = v.get_uint64();
      if (unlikely(ures.error())) raise_simdjson_error_with_reparse(mrb, doc, ures.error());
      return mrb_convert_number(mrb, static_cast<uint64_t>(ures.value()));
    }

    case number_type::signed_integer: {
      auto ires = v.get_int64();
      if (unlikely(ires.error())) raise_simdjson_error_with_reparse(mrb, doc, ires.error());
      return mrb_convert_number(mrb, static_cast<int64_t>(ires.value()));
    }

    case number_type::floating_point_number: {
      auto dres = v.get_double();
      if (unlikely(dres.error())) raise_simdjson_error_with_reparse(mrb, doc, dres.error());
      return mrb_convert_number(mrb, static_cast<double>(dres.value()));
    }

    case number_type::big_integer: {
#if !defined(__SIZEOF_INT128__)
      mrb_raise(mrb, E_JSON_BIGINT_ERROR, "128 bit integers are not supported");
#else
      return convert_big_integer_from_ondemand(mrb, doc, v);
#endif
    }

    default:
      mrb_raise(mrb, E_JSON_NUMBER_ERROR, "unknown number type");
  }
}

static mrb_value
convert_string_from_ondemand(mrb_state* mrb, mrb_json_doc *doc, ondemand::value& v)
{
  // 1. Get raw JSON slice (includes quotes + escapes)
  auto raw_json = v.raw_json();
  if (unlikely(raw_json.error())) {
      raise_simdjson_error_with_reparse(mrb, doc, raw_json.error());
  }
  auto raw = raw_json.value();
  
  // raw = "\"...\"" including quotes
  const char* raw_start = raw.data() + 1;     // inside opening quote
  size_t raw_len = raw.size() - 2;            // exclude both quotes
  
  // 2. Compute offset into original buffer
  // current_location() points at the opening quote in the original buffer
  const char* loc = v.current_location();
  size_t offset = (loc - doc->buffer.data()) + 1;
  
  // 3. Fast path: slice raw UTF-8 directly from original source
  mrb_value fast = mrb_str_substr(mrb, doc->source, offset, mrb_utf8_strlen(raw_start, raw_len));
  
  // 4. Decode using simdjson (slow path)
  auto decoded = v.get_string();
  if (unlikely(decoded.error())) raise_simdjson_error_with_reparse(mrb, doc, decoded.error());
  auto dec = decoded.value();
  
  // 5. Compare sizes
  if (dec.size() == raw_len) {
      return fast;
  }
  // 6. Escapes present → use decoded UTF-8
  return mrb_str_new(mrb, dec.data(), dec.size());
}

static mrb_value
convert_ondemand_value_to_mrb(mrb_state* mrb, mrb_json_doc *doc, ondemand::value& v)
{
  using namespace ondemand;
  switch (v.type()) {
    case json_type::object:
      return convert_ondemand_object(mrb, doc, v.get_object());
    case json_type::array:
      return convert_ondemand_array(mrb, doc, v.get_array());
    case json_type::string:
      return convert_string_from_ondemand(mrb, doc, v);
    case json_type::number:
      return convert_number_from_ondemand(mrb, doc, v);
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

static mrb_json_doc*
mrb_json_doc_get(mrb_state* mrb, mrb_value self)
{
  auto* doc = mrb_cpp_get<mrb_json_doc>(mrb, self);
  if (likely(!doc->need_to_reparse)) return doc;

  auto result = doc->parser.iterate(doc->buffer);
  if (unlikely(result.error())) raise_simdjson_error_with_reparse(mrb, doc, result.error());

  doc->doc = std::move(result.value());
  doc->need_to_reparse = false;

  return doc;
}

static mrb_value
mrb_json_doc_aref(mrb_state* mrb, mrb_value self)
{
  mrb_value key;
  mrb_get_args(mrb, "S", &key);

  auto* doc = mrb_json_doc_get(mrb, self);

  std::string_view k(RSTRING_PTR(key), RSTRING_LEN(key));
  ondemand::value val;
  auto err = doc->doc[k].get(val);
  if (unlikely(err != SUCCESS)) raise_simdjson_error_with_reparse(mrb, doc, err);

  return convert_ondemand_value_to_mrb(mrb, doc, val);
}

static mrb_value
mrb_json_doc_at(mrb_state* mrb, mrb_value self)
{
  mrb_int index;
  mrb_get_args(mrb, "i", &index);

  auto* doc = mrb_json_doc_get(mrb, self);

  ondemand::value val;
  auto err = doc->doc.at(index).get(val);
  if (unlikely(err != SUCCESS)) raise_simdjson_error_with_reparse(mrb, doc, err);

  return convert_ondemand_value_to_mrb(mrb, doc, val);
}

static mrb_value
mrb_json_doc_at_pointer(mrb_state* mrb, mrb_value self)
{
  mrb_value ptr_val;
  mrb_get_args(mrb, "S", &ptr_val);

  auto* doc = mrb_json_doc_get(mrb, self);

  std::string_view json_pointer(RSTRING_PTR(ptr_val), RSTRING_LEN(ptr_val));
  auto vres = doc->doc.at_pointer(json_pointer);
  if (vres.error()) {
    raise_simdjson_error_with_reparse(mrb, doc, vres.error());
  }

  ondemand::value val = vres.value();
  return convert_ondemand_value_to_mrb(mrb, doc, val);
}

static mrb_value
mrb_json_doc_at_path(mrb_state* mrb, mrb_value self)
{
  mrb_value path_val;
  mrb_get_args(mrb, "S", &path_val);

  auto* doc = mrb_json_doc_get(mrb, self);

  std::string_view json_path(RSTRING_PTR(path_val), RSTRING_LEN(path_val));
  auto vres = doc->doc.at_path(json_path);
  if (vres.error()) raise_simdjson_error_with_reparse(mrb, doc, vres.error());

  return convert_ondemand_value_to_mrb(mrb, doc, vres.value());
}

static mrb_value
mrb_json_doc_at_path_with_wildcard(mrb_state* mrb, mrb_value self)
{
  mrb_value path_val;
  mrb_get_args(mrb, "S", &path_val);

  auto* doc = mrb_json_doc_get(mrb, self);

  std::string_view json_path(RSTRING_PTR(path_val), RSTRING_LEN(path_val));
  std::vector<ondemand::value> values;
  auto error = doc->doc.at_path_with_wildcard(json_path).get(values);
  if (error != SUCCESS) raise_simdjson_error_with_reparse(mrb, doc, error);

  mrb_value ary = mrb_ary_new(mrb);
  int arena = mrb_gc_arena_save(mrb);
  for (auto v : values) {
    mrb_ary_push(mrb, ary, convert_ondemand_value_to_mrb(mrb, doc, v));
    mrb_gc_arena_restore(mrb, arena);
  }
  return ary;

}

static mrb_value
mrb_json_doc_array_each(mrb_state* mrb, mrb_value self)
{
  mrb_value block = mrb_nil_value();
  mrb_get_args(mrb, "&", &block);

  auto* doc = mrb_json_doc_get(mrb, self);
  auto arr = doc->doc.get_array();
  if (unlikely(arr.error())) raise_simdjson_error_with_reparse(mrb, doc, arr.error());

  int arena = mrb_gc_arena_save(mrb);
  for (ondemand::value v : arr.value()) {
    mrb_value ruby_val = convert_ondemand_value_to_mrb(mrb, doc, v);
    mrb_yield(mrb, block, ruby_val);
    mrb_gc_arena_restore(mrb, arena);
  }

  return self;
}


static mrb_value
mrb_json_doc_iterate(mrb_state* mrb, mrb_value self)
{
  auto* doc = mrb_cpp_get<mrb_json_doc>(mrb, self);

  auto result = doc->parser.iterate(doc->buffer);
  if (unlikely(result.error())) raise_simdjson_error_with_reparse(mrb, doc, result.error());
  doc->doc = std::move(result.value());

  return self; // allow chaining
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
                        void *data) {
  auto *ctx = static_cast<DumpHashCtx *>(data);

  if (ctx->first)
    ctx->first = false;
  else
    ctx->builder.append_comma();

  json_encode(mrb, key, ctx->builder);
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
  mrb_int n = RARRAY_LEN(v);

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
  if (unlikely(!sb.validate_unicode())) {
    mrb_raise(mrb, E_JSON_UTF8_ERROR, "invalid utf-8");
  }
  std::string_view sv = sb.view();
  return mrb_str_new(mrb, sv.data(), sv.size());
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

MRB_BEGIN_DECL
void mrb_mruby_fast_json_gem_init(mrb_state *mrb) {

#ifdef _WIN32
  SYSTEM_INFO sysInfo;
  GetSystemInfo(&sysInfo);
  pagesize = sysInfo.dwPageSize;
#else
  pagesize = sysconf(_SC_PAGESIZE);
#endif

  struct RClass *json_mod = mrb_define_module_id(mrb, MRB_SYM(JSON));
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

  mrb_define_class_method_id(mrb, json_mod, MRB_SYM(parse), mrb_json_parse_m,
                             MRB_ARGS_REQ(1) | MRB_ARGS_KEY(1, 0));
  mrb_define_class_method_id(mrb, json_mod, MRB_SYM(dump), mrb_json_dump_m,
                             MRB_ARGS_REQ(1));
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


  struct RClass* doc =
    mrb_define_class_under_id(mrb, json_mod, MRB_SYM(Document), mrb->object_class);
  MRB_SET_INSTANCE_TT(doc, MRB_TT_DATA);

  mrb_define_method_id(mrb, doc, MRB_SYM(initialize),
                      mrb_json_doc_initialize, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, doc, MRB_OPSYM(aref),
                      mrb_json_doc_aref, MRB_ARGS_REQ(1));
  mrb_define_method_id( mrb, doc, MRB_SYM(at),
                      mrb_json_doc_at, MRB_ARGS_REQ(1));
  mrb_define_method_id( mrb, doc, MRB_SYM(at_pointer),
                      mrb_json_doc_at_pointer, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, doc, MRB_SYM(at_path),
                      mrb_json_doc_at_path, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, doc, MRB_SYM(at_path_with_wildcard),
                      mrb_json_doc_at_path_with_wildcard, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, doc, MRB_SYM(iterate),
                      mrb_json_doc_iterate, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, doc, MRB_SYM(array_each),
                      mrb_json_doc_array_each, MRB_ARGS_BLOCK());
                      mrb_value s = mrb_str_new_lit(mrb, "hello"); printf("%zu\n", RSTRING_CAPA(s)); // prints 0
}

void mrb_mruby_fast_json_gem_final(mrb_state *mrb) {}
MRB_END_DECL
