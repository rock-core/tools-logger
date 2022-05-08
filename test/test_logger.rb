# frozen_string_literal: true

require "minitest/autorun"
require "orocos/test/component"

require "pocolog"
require "fileutils"
require "time"

class TC_BasicBehaviour < Minitest::Test
    include Orocos::Test::Component
    start "logger", "rock_logger"

    def task
        logger
    end

    def setup
        super

        @tempdir = Dir.mktmpdir
        @tempfiles = []

        task.overwrite_existing_files = true
        task.auto_timestamp_files = false
        task.file = make_tmppath
    end

    def teardown
        super

        FileUtils.rm_rf @tempdir
        @logfile_io&.close
    end

    def make_tmppath(suffix: ".log")
        @tempfiles << File.join(@tempdir, "#{@tempfiles.size}#{suffix}")
        @tempfiles.last
    end

    def logfile_path
        @logfile_io ||= File.open(task.current_file)
        @logfile_io.path
    end

    def logfile
        @logfile ||= Pocolog::Logfiles.open(logfile_path)
    end

    def generate_and_check_logfile
        assert(task.has_port?("time"))

        task.configure
        task.start
        # create file for logging
        logfile_path

        writer = task.time.writer(type: :buffer, size: 2000)
        expected = []
        10.times do |i|
            expected << Time.at(i)
            writer.write(expected.last)
        end
        sleep 0.1

        # Make sure the I/O is properly flushed
        task.stop
        task.cleanup

        temp_log_file = make_tmppath
        FileUtils.cp logfile_path, temp_log_file
        @logfile = Pocolog::Logfiles.open(temp_log_file)
        stream = logfile.stream("time")
        samples = stream.samples.to_a.map(&:last)
        assert_equal(expected, samples)
        stream
    end

    def test_basics
        assert(!task.has_port?("time"))
        assert(task.createLoggingPort("time", "/base/Time", []))
        generate_and_check_logfile
    end

    def test_conflicting_properties_expect_transition_error
        task.overwrite_existing_files = true
        task.auto_timestamp_files = true
        task.configure
        assert_raises Orocos::StateTransitionFailed do
            task.start
        end
    end

    def test_no_overwrite_expect_transition_error
        task.overwrite_existing_files = false
        task.auto_timestamp_files = false
        task.configure
        FileUtils.touch task.file
        assert_raises Orocos::StateTransitionFailed do
            task.start
        end
    end

    def test_auto_timestamp_file
        task.overwrite_existing_files = false
        task.auto_timestamp_files = true
        assert(!task.has_port?("time"))
        assert(task.createLoggingPort("time", "/base/Time", []))
        generate_and_check_logfile
        assert(task.file != task.current_file)
    end

    def test_re_stamping_existing_timestamped_file
        task.overwrite_existing_files = false
        task.auto_timestamp_files = true
        assert(!task.has_port?("time"))
        assert(task.createLoggingPort("time", "/base/Time", []))
        task.configure
        task.start
        task.stop
        generate_and_check_logfile
        assert(task.file != task.current_file)
    end

    def test_no_suffix_log_file
        task.overwrite_existing_files = false
        task.auto_timestamp_files = true
        task.file = make_tmppath(suffix: "")
        assert(!task.has_port?("time"))
        assert(task.createLoggingPort("time", "/base/Time", []))
        generate_and_check_logfile
        assert(task.file == task.current_file.split(".")[0])
    end

    def test_metadata
        assert(!task.has_port?("time"))
        meta = []
        meta << Hash["key" => "key0", "value" => "value0"]
        meta << Hash["key" => "key1", "value" => "value1"]
        assert(task.createLoggingPort("time", "/base/Time", meta))
        stream = generate_and_check_logfile
        assert_equal(
            { "key0" => "value0",
              "key1" => "value1",
              "rock_cxx_type_name" => "/base/Time" },
            stream.metadata
        )
    end

    def test_create_port_log
        source = new_ruby_task_context "source"
        source.create_output_port "out", "/int32_t"
        task.log(source.out)
        assert(task.has_port?("source.out"))
        task.configure
        task.start
        # create file for logging
        logfile_path

        task.stop

        stream = logfile.stream("source.out")
        expected_metadata = {
            "rock_stream_type" => "port",
            "rock_task_model" => nil,
            "rock_task_name" => "source",
            "rock_task_object_name" => "out",
            "rock_orocos_type_name" => "/int32_t",
            "rock_cxx_type_name" => "/int32_t"
        }
        assert_equal expected_metadata, stream.metadata
    end

    def test_log_port
        # Create a ruby task as source
        source = new_ruby_task_context "source"
        source.create_output_port "out", "/int"
        task.log(source.out)
        task.configure
        task.start
        logfile_path

        source.out.write 1
        source.out.write 2
        source.out.write 3
        sleep 0.1

        task.stop
        stream = logfile.stream("source.out")
        assert_equal [1, 2, 3], stream.samples.to_a.map(&:last)
    end

    def test_create_property_log
        source = new_ruby_task_context "source"
        source.create_property "file", "/std/string"
        task.create_log(source.property("file"))
        assert(task.has_port?("source.file"))
        task.configure
        task.start
        logfile_path
        task.stop

        stream = logfile.stream("source.file")
        expected_metadata = {
            "rock_stream_type" => "property",
            "rock_task_model" => nil,
            "rock_task_name" => "source",
            "rock_task_object_name" => "file",
            "rock_orocos_type_name" => "/std/string",
            "rock_cxx_type_name" => "/std/string"
        }
        assert_equal expected_metadata, stream.metadata
    end

    def test_log_property
        source = new_ruby_task_context "source"
        source.create_property "file", "/std/string"
        source.file = "test"
        task.log(source.property("file"))
        task.configure
        task.start
        logfile_path

        source.file = "bla.0.log"
        sleep 0.1

        task.stop

        stream = logfile.stream("source.file")
        assert_equal ["test", "bla.0.log"], stream.samples.to_a.map(&:last)
    end

    def test_it_can_configure_and_start_in_no_overwrite_mode
        task.overwrite_existing_files = false
        task.configure
        task.start
        task.stop
    end

    def test_it_does_not_create_the_file_before_it_gets_started
        refute File.file?(task.file)
        task.configure
        refute File.file?(task.file)
        task.file = task.file
        refute File.file?(task.file)
        task.start
        assert File.file?(task.file)
    end

    def test_it_supports_the_file_property_being_set_between_configure_and_start_with_overwriting_false
        task.overwrite_existing_files = false
        task.configure
        task.file = task.file
        task.start
    end

    def test_change_file_auto_timestamp
        task.overwrite_existing_files = false
        task.auto_timestamp_files = true

        base_path = make_tmppath(suffix: "")
        task.file = base_path

        task.configure
        task.start
        initial_current_file = task.current_file
        assert initial_current_file.start_with?(base_path)
        initial_timestamp = initial_current_file.split(".")[1]

        task.file = base_path
        refute_equal initial_current_file, task.current_file
        assert task.current_file.start_with?(base_path)
        final_timestamp = task.current_file.split(".")[1]
        task.stop

        # NOTE: the way the timestamps are generated allows for string comparison
        assert initial_timestamp < final_timestamp
    end

    def test_change_file_plain
        task.overwrite_existing_files = false
        task.auto_timestamp_files = false

        task.configure
        task.start
        initial_path = make_tmppath
        task.file = initial_path
        assert_equal initial_path, task.current_file

        new_path = make_tmppath
        task.file = new_path
        assert_equal new_path, task.current_file
        task.stop
    end

    def test_log_new_file
        source = new_ruby_task_context "source"
        source.create_output_port "out", "/int"
        task.log(source.out)
        task.configure
        task.start
        path1 = File.open(task.current_file).path

        source.out.write 1
        source.out.write 2
        source.out.write 3
        sleep 0.1

        task.file = make_tmppath
        path2 = File.open(task.current_file).path

        source.out.write 4
        source.out.write 5
        source.out.write 6
        sleep 0.1

        task.stop
        stream1 = Pocolog::Logfiles.open(path1).stream("source.out")
        stream2 = Pocolog::Logfiles.open(path2).stream("source.out")
        assert_equal [1, 2, 3], stream1.samples.to_a.map(&:last)
        assert_equal [4, 5, 6], stream2.samples.to_a.map(&:last)
    end

    def test_keep_previous_file_when_error_occurs
        task.configure
        task.start
        logfile_path = task.current_file

        task.overwrite_existing_files = false
        task.auto_timestamp_files = false

        file_path = make_tmppath
        FileUtils.touch file_path
        assert_raises Orocos::PropertyChangeRejected do
            task.file = file_path
        end
        assert_equal logfile_path, task.current_file
        assert_equal logfile_path, task.file
        task.stop
    end
end
