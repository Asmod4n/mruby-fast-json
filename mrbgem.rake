MRuby::Gem::Specification.new('mruby-fast-json') do |spec|
  cc_ok  = spec.build.cc.defines.include?('MRB_UTF8_STRING')
  cxx_ok = spec.build.cxx.defines.include?('MRB_UTF8_STRING')

  unless cc_ok
    fail <<~MSG
      mruby-fast-json requires MRB_UTF8_STRING for MRuby core.
      Add this to your build_config.rb:

        conf.cc.defines  << 'MRB_UTF8_STRING'
        conf.cxx.defines << 'MRB_UTF8_STRING'
    MSG
  end

  unless cxx_ok
    fail <<~MSG
      mruby-fast-json requires MRB_UTF8_STRING for C++ sources.
      Add this to your build_config.rb:

        conf.cxx.defines << 'MRB_UTF8_STRING'
    MSG
  end

  spec.license = 'Apache-2'
  spec.author  = 'Hendrik Beskow'
  spec.summary = 'simdjson for mruby'
  spec.add_dependency 'mruby-bigint'
  spec.add_dependency 'mruby-c-ext-helpers'
  spec.add_dependency 'mruby-chrono'
  spec.add_dependency 'mruby-native-ext-type', :github => 'Asmod4n/mruby-native-ext-type', branch: "main"
  spec.add_test_dependency 'mruby-io'
  spec.cc.defines  << 'MRB_USE_BIGINT'
  spec.cxx.defines << 'MRB_USE_BIGINT'
  # A build that already asks for C++20 or later keeps its -std: the
  # last -std on the line wins, and a later one here would take away
  # what the build chose, -freflection's C++26 among it.
  cxx20_or_later = spec.cxx.flags.flatten.any? do |flag|
    version = flag.to_s[%r{\A[-/]std[:=](?:c|gnu)\+\+(\w+)\z}, 1]
    version == 'latest' || version.to_s.match?(/\A2[0-9a-z]\z/)
  end
  spec.cxx.flags << (spec.for_windows? ? '/std:c++20' : '-std=c++20') unless cxx20_or_later

  unless spec.cxx.defines.include? 'MRB_DEBUG'
    spec.cxx.flags << '-O3'
    spec.cxx.defines << 'NDEBUG' << '__OPTIMIZE__=1'
  end
end
