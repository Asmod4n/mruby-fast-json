MRuby::Gem::Specification.new('mruby-fast-json') do |spec|
  spec.license = 'Apache-2'
  spec.author  = 'Hendrik Beskow'
  spec.summary = 'simdjson for mruby'
  spec.add_dependency 'mruby-bigint'
  spec.add_dependency 'mruby-chrono'
  spec.add_dependency 'mruby-c-ext-helpers'
  if spec.for_windows?
    spec.cxx_flags << '/std=c++17'
  else
    spec.cxx.flags << '-std=c++17'
  end
  unless spec.cxx.defines.include? 'MRB_DEBUG'
    spec.cxx.flags << '-O3'
    spec.cxx.defines << 'NDEBUG' << '__OPTIMIZE__=1'
  end

  simdjson_src = File.expand_path("#{spec.dir}/deps/simdjson/singleheader", __dir__)

  spec.cxx.include_paths << simdjson_src
  source_files = %W(
    #{simdjson_src}/simdjson.cpp
  )
  spec.objs += source_files.map { |f| f.relative_path_from(dir).pathmap("#{build_dir}/%X#{spec.exts.object}" ) }
end
