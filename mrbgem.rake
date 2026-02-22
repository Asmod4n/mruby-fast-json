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
      spec.cc.defines  << 'MRB_USE_BIGINT'
    spec.cxx.defines << 'MRB_USE_BIGINT'

  unless spec.cxx.defines.include? 'MRB_DEBUG'
    spec.cxx.flags << '-O3' << "-march=native"
    spec.cxx.defines << 'NDEBUG' << '__OPTIMIZE__=1'
  end

  simdjson_src = File.expand_path("#{spec.dir}/deps/simdjson/singleheader", __dir__)

  spec.cxx.include_paths << simdjson_src
  source_files = %W(
    #{simdjson_src}/simdjson.cpp
  )
  spec.objs += source_files.map { |f| f.relative_path_from(dir).pathmap("#{build_dir}/%X#{spec.exts.object}" ) }
end
