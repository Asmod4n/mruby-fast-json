assert("JSON.parse - valid simple object") do
  obj = JSON.parse('{"name":"Alice","age":30}')
  assert_equal "Alice", obj["name"]
  assert_equal 30, obj["age"]
end

assert("JSON.parse - valid array of mixed types") do
  arr = JSON.parse('[true, null, 42, "hi"]')
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
  inp = "\x01\x02\x1F"
  out = JSON.dump(inp)
  assert_equal "\"\\u0001\\u0002\\u001F\"", out
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

# 7. invalid-UTF-8 bytes → mrb_utf8len()<1 fallback
assert("invalid UTF-8 bytes are treated as raw single-bytes") do
  # embed 0xC0 which is illegal in UTF-8
  inp = "\xC0".dup
  out = JSON.dump(inp)
  # since 0xC0 ≥ 0x20 and no mapping, it’s emitted verbatim
  assert_equal "\"\xC0\"", out
end
