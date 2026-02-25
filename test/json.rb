assert("JSON.parse - valid simple object") do
  obj = JSON.parse('{"name":"Alice","age":30}')
  assert_equal "Alice", obj["name"]
  assert_equal 30, obj["age"]
end

assert("JSON.parse - simple object with symbol keys") do
  obj = JSON.parse('{"name":"Alice","age":30}', symbolize_names: true)
  assert_equal "Alice", obj[:name]
  assert_equal 30, obj[:age]
  assert_nil obj["name"] # ensure string keys aren't present
end

assert("JSON.parse - nested object with symbol keys") do
  obj = JSON.parse('{"user":{"id":1,"name":"Bob"}}', symbolize_names: true)
  assert_equal({ id: 1, name: "Bob" }, obj[:user])
end

assert("JSON.parse - roundtrip with symbol keys preserved") do
  original = { nested: { emoji: "😀" } }
  json = JSON.dump(original)
  obj  = JSON.parse(json, symbolize_names: true)
  assert_equal "😀", obj[:nested][:emoji]
end

assert("JSON.parse - symbol key with UTF-8 content") do
  obj = JSON.parse('{"挨拶":"こんにちは"}', symbolize_names: true)
  assert_equal "こんにちは", obj[:"挨拶"]
end

assert("JSON.parse - mixed keys: symbol access only") do
  obj = JSON.parse('{"a":1,"b":2}', symbolize_names: true)
  keys = obj.keys
  assert_equal [:a, :b], keys
end

assert("JSON.parse - empty object with symbol keys") do
  obj = JSON.parse('{}', symbolize_names: true)
  assert_equal({}, obj)
end


assert("JSON.parse - valid array of mixed types") do
  arr = JSON.parse('[true, null, 42, "hi"]'.freeze)
  assert_equal true, arr[0]
  assert_nil arr[1]
  assert_equal 42, arr[2]
  assert_equal "hi", arr[3]
end

assert("JSON.parse - nested object") do
  obj = JSON.parse('{"user":{"id":1,"name":"Bob"}}')
  assert_equal({"id"=>1, "name"=>"Bob"}, obj["user"])
end

assert("JSON.parse - UTF-8 string") do
  obj = JSON.parse('{"greeting":"こんにちは"}')
  assert_equal "こんにちは", obj["greeting"]
end

assert("JSON.parse - empty array and object") do
  assert_equal [], JSON.parse("[]")
  assert_equal({}, JSON.parse("{}"))
end

assert("JSON.parse - error: trailing comma") do
  assert_raise JSON::ParserError do
    JSON.parse('{"a":1,}')
  end
end

assert("JSON.parse - error: unquoted key") do
  assert_raise JSON::ParserError do
    JSON.parse('{key:"value"}')
  end
end

assert("JSON.dump - round trip simple object") do
  obj = {"x" => 1, "y" => "z"}
  json = JSON.dump(obj)
  assert_equal '{"x":1,"y":"z"}', json
end

assert("JSON.dump - array serialization") do
  arr = [true, nil, "text"]
  json = JSON.dump(arr)
  assert_equal '[true,null,"text"]', json
end

assert("JSON.dump - deeply nested structure") do
  obj = {"a" => {"b" => {"c" => {"d" => 42}}}}
  json = JSON.dump(obj)
  assert_include json, '"d":42'
end

assert("JSON.parse - TapeError") do
  assert_raise JSON::TapeError do
    JSON.parse('true garbage')
  end
end

assert("JSON.parse - StringError") do
  assert_raise JSON::StringError do
    JSON.parse('"Line\\qBreak"')
  end
end

assert("JSON.parse - UnclosedStringError") do
  assert_raise JSON::UnclosedStringError do
    JSON.parse('"unterminated')
  end
end

assert("JSON.parse - DepthError") do
  deep = '{"a":' * 2048 + '1' + '}' * 2048
  assert_raise JSON::DepthError do
    JSON.parse(deep)
  end
end

assert("JSON.parse - StringError") do
  assert_raise JSON::StringError do
    JSON.parse('"\xC0"')
  end
end

assert("JSON.parse - NumberError") do
  assert_raise JSON::NumberError do
    JSON.parse('{"x":12.3.4}')
  end
end

assert("JSON.parse - TapeError") do
  assert_raise JSON::TapeError do
    JSON.parse('{key:"value"}')
  end
end

assert("JSON.parse - EmptyInputError") do
  assert_raise JSON::EmptyInputError do
    JSON.parse('')
  end
end

assert("JSON.dump/parse roundtrip preserves UTF-8") do
  original = {
    "japanese" => "こんにちは",
    "emoji"    => "😀😃😄",
    "nested"   => ["δοκιμή", { "русский" => "тест" }]
  }
  json   = JSON.dump(original)
  result = JSON.parse(json)

  assert_equal original, result
end

assert("String#valid_utf8? detects bad sequences") do
  # raw invalid bytes in JSON literal
  bad_json = "\"\xC0\xAF\""
  assert_raise JSON::UTF8Error do
    JSON.parse(bad_json)
  end
end

# test/json_escape_paths.rb

# 1. printable ASCII → pass-through
assert("printable ASCII stays unescaped") do
  inp = "Hello, world!"
  out = JSON.dump(inp)
  assert_equal "\"Hello, world!\"", out
end

# 2. JSON_ESCAPE_MAPPING chars: quotes and backslashes
assert("quotes and backslashes get \\\" or \\\\ escapes") do
  inp = "\"\\"
  out = JSON.dump(inp)
  assert_equal "\"\\\"\\\\\"", out
end

# 3. control chars with dedicated escapes: \b \f \n \r \t
assert("standard control chars") do
  inp = "\b\f\n\r\t"
  out = JSON.dump(inp)
  assert_equal "\"\\b\\f\\n\\r\\t\"", out
end

# 4. other C0 controls (<0x20) → \u00XX
assert("other C0 controls → \\u00XX") do
  inp = "\x01\x02\x1f"
  out = JSON.dump(inp)
  assert_equal "\"\\u0001\\u0002\\u001f\"", out
end

# 5. mixed ASCII, escapes, C0, and UTF-8 in one string
assert("mixed ASCII+escapes+C0+UTF-8") do
  utf8 = "λ😀"
  inp  = "\"\b" + utf8 + "\n"
  out  = JSON.dump(inp)
  expect = "\"\\\"\\b#{utf8}\\n\""
  assert_equal expect, out
end

# 6. pure multi-byte UTF-8 (no escaping)
assert("multi-byte UTF-8 passes through unmodified") do
  inp = "δοκιμή Русский 漢字"
  out = JSON.dump(inp)
  assert_equal "\"#{inp}\"", out
end

assert("JSON.parse - uint64_t larger than INT64_MAX becomes BigInt") do
  # 2^63 = 9223372036854775808 (INT64_MAX + 1)
  json = '{"big":9223372036854775808}'
  obj = JSON.parse(json)
  val = obj["big"]

  assert_equal 9223372036854775808, val
end

assert("JSON.parse - TAtomError") do
  assert_raise JSON::TAtomError do
    JSON.parse('tru')
  end
end

assert("JSON.parse - FAtomError") do
  assert_raise JSON::FAtomError do
    JSON.parse('fals')
  end
end

assert("JSON.parse - NAtomError") do
  assert_raise JSON::NAtomError do
    JSON.parse('nul')
  end
end

assert("JSON.parse - BigIntError") do
  huge = '{"x":' + ('9' * 20000) + '}'
  assert_raise JSON::BigIntError do
    JSON.parse(huge)
  end
end

assert("JSON.parse - BigIntError") do
  huge = '{"x":' + ('9' * 20000) + '}'
  assert_raise JSON::BigIntError do
    JSON.parse(huge)
  end
end
assert("JSON.parse - UnescapedCharsError") do
  json = "\"hello\u0001world\""
  assert_raise JSON::UnescapedCharsError do
    JSON.parse(json)
  end
end


# ---------------------------------------------------------
# JSON.parse_lazy — basic functionality
# ---------------------------------------------------------

assert("JSON.parse_lazy - simple object") do
  doc = JSON.parse_lazy('{"name":"Alice","age":30}')
  assert_equal "Alice", doc["name"]
  assert_equal 30, doc["age"]
end

assert("JSON.parse_lazy - nested object") do
  doc = JSON.parse_lazy('{"user":{"id":1,"name":"Bob"}}')
  user = doc["user"]
  assert_equal 1, user["id"]
  assert_equal "Bob", user["name"]
end

assert("JSON.parse_lazy - array access via .at") do
  doc = JSON.parse_lazy('[true,null,42,"hi"]')
  assert_equal true, doc.at(0)
end

assert("JSON.parse_lazy - array access via .at after rewind") do
  doc = JSON.parse_lazy('[true,null,42,"hi"]')
  doc.at(0)
  doc.rewind
  assert_equal nil, doc.at(1)
end


assert("JSON.parse_lazy - UTF-8 string") do
  doc = JSON.parse_lazy('{"greeting":"こんにちは"}')
  assert_equal "こんにちは", doc["greeting"]
end

assert("JSON.parse_lazy - empty array and object") do
  assert_equal [], JSON.parse_lazy("[]").array_each
  assert_equal({}, JSON.parse_lazy("{}").object_each)
end

# ---------------------------------------------------------
# Lookup semantics
# ---------------------------------------------------------

assert("JSON.parse_lazy - lookup miss returns nil") do
  doc = JSON.parse_lazy('{"a":1}')
  assert_nil doc["missing"]
end

assert("JSON.parse_lazy - fetch raises KeyError") do
  doc = JSON.parse_lazy('{"a":1}')
  assert_raise KeyError do
    doc.fetch("missing")
  end
end

assert("JSON.parse_lazy - fetch index raises IndexError") do
  doc = JSON.parse_lazy('[1,2,3]')
  assert_raise IndexError do
    doc.fetch(10)
  end
end

# ---------------------------------------------------------
# JSON Pointer / Path
# ---------------------------------------------------------

assert("JSON.parse_lazy - at_pointer") do
  doc = JSON.parse_lazy('{"user":{"name":"Alice"}}')
  assert_equal "Alice", doc.at_pointer("/user/name")
end

assert("JSON.parse_lazy - at_path") do
  doc = JSON.parse_lazy('{"user":{"id":7}}')
  assert_equal 7, doc.at_path("$.user.id")
end

assert("JSON.parse_lazy - at_path_with_wildcard") do
  doc = JSON.parse_lazy('{"items":[{"id":1},{"id":2},{"id":3}]}')
  ids = doc.at_path_with_wildcard("$.items[*].id")
  assert_equal [1,2,3], ids
end

# ---------------------------------------------------------
# Iteration
# ---------------------------------------------------------

assert("JSON.parse_lazy - array_each") do
  doc = JSON.parse_lazy('[1,2,3]')
  out = []
  doc.array_each { |v| out << v }
  assert_equal [1,2,3], out
end

assert("JSON.parse_lazy - object_each") do
  doc = JSON.parse_lazy('{"a":1,"b":2}')
  h = {}
  doc.object_each { |k,v| h[k] = v }
  assert_equal({"a"=>1,"b"=>2}, h)
end

# ---------------------------------------------------------
# Rewind
# ---------------------------------------------------------

assert("JSON.parse_lazy - rewind allows re-reading") do
  doc = JSON.parse_lazy('{"a":1,"b":2}')
  assert_equal 1, doc["a"]
  doc.rewind
  assert_equal 2, doc["b"]
end

# ---------------------------------------------------------
# load_lazy (file loading)
# ---------------------------------------------------------

assert("JSON.load_lazy - loads file lazily") do
  File.new("tmp.json", "w").write('{"x":123}')
  doc = JSON.load_lazy("tmp.json")
  assert_equal 123, doc["x"]
end

assert("JSON.load_lazy - large file streaming") do
  File.new("tmp2.json", "w").write('{"items":[1,2,3,4]}')
  doc = JSON.load_lazy("tmp2.json")
  assert_equal [1,2,3,4], doc["items"]
end

# ---------------------------------------------------------
# native_ext_deserialize
# ---------------------------------------------------------

assert("JSON.parse_lazy - native_ext_deserialize simple") do
  class Foo
    attr_accessor :foo
    native_ext_deserialize :@foo, JSON::Type::String
  end

  doc = JSON.parse_lazy('{"foo":"hello"}')
  foo = Foo.new
  doc.into(foo)

  assert_equal "hello", foo.foo
end

assert("JSON.parse_lazy - native_ext_deserialize type mismatch") do
  class Bar
    attr_accessor :x
    native_ext_deserialize :@x, JSON::Type::Number
  end

  doc = JSON.parse_lazy('{"x":"not an int"}')
  bar = Bar.new

  assert_raise TypeError do
    doc.into(bar)
  end
end

assert("JSON.parse_lazy - native_ext_deserialize partial match") do
  class Baz
    attr_accessor :a, :b
    native_ext_deserialize :@a, JSON::Type::Number
    native_ext_deserialize :@b, JSON::Type::String
  end

  doc = JSON.parse_lazy('{"a":1,"b":"ok"}')
  baz = Baz.new
  doc.into(baz)

  assert_equal 1, baz.a
  assert_equal "ok", baz.b
end
