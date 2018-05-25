#include <iostream>
#include <vector>

#include <opencv2/highgui.hpp>
#include <opencv2/video.hpp>
#include "clipp.h"

extern "C" 
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

using namespace clipp;
using namespace std;

cv::VideoCapture get_device(string camID, double width, double height)
{
  cv::VideoCapture cam;
  if(cam.open(camID))
  {
	  cout<<"open "<<camID<<" success!"<<endl;
  }
  else if (cam.open(atoi(camID.c_str())))
  {
    cout << " open video capture device!" << endl;
  }

  cam.set(cv::CAP_PROP_FRAME_WIDTH, width);
  cam.set(cv::CAP_PROP_FRAME_HEIGHT, height);

  return cam;
}

void initialize_avformat_context(AVFormatContext *&fctx, const char *format_name)
{
  int ret = avformat_alloc_output_context2(&fctx, nullptr, format_name, nullptr);
  if (ret < 0)
  {
    cout << "Could not allocate output format context!" << endl;
    exit(1);
  }
}

void initialize_io_context(AVFormatContext *&fctx, const char *output)
{
  if (!(fctx->oformat->flags & AVFMT_NOFILE))
  {
    int ret = avio_open2(&fctx->pb, output, AVIO_FLAG_WRITE, nullptr, nullptr);
    if (ret < 0)
    {
      cout << "Could not open output IO context!" << endl;
      exit(1);
    }
  }
}

void set_codec_params(AVFormatContext *&fctx, AVCodecContext *&codec_ctx, double width, double height, int fps, int bitrate)
{
  const AVRational dst_fps = {fps, 1};

  codec_ctx->codec_tag = 0;
  codec_ctx->codec_id = AV_CODEC_ID_H264;
  codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
  codec_ctx->width = width;
  codec_ctx->height = height;
  codec_ctx->gop_size = 12;
  codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  codec_ctx->framerate = dst_fps;
  codec_ctx->time_base = av_inv_q(dst_fps);
  codec_ctx->bit_rate = bitrate;
  if (fctx->oformat->flags & AVFMT_GLOBALHEADER)
  {
    codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
}

void initialize_codec_stream(AVStream *&stream, AVCodecContext *&codec_ctx, AVCodec *&codec, string codec_profile)
{
  int ret = avcodec_parameters_from_context(stream->codecpar, codec_ctx);
  if (ret < 0)
  {
    cout << "Could not initialize stream codec parameters!" << endl;
    exit(1);
  }

  AVDictionary *codec_options = nullptr;
  av_dict_set(&codec_options, "profile", codec_profile.c_str(), 0);
  av_dict_set(&codec_options, "preset", "superfast", 0);
  av_dict_set(&codec_options, "tune", "zerolatency", 0);

  // open video encoder
  ret = avcodec_open2(codec_ctx, codec, &codec_options);
  if (ret < 0)
  {
    cout << "Could not open video encoder!" << endl;
    exit(1);
  }
}

SwsContext *initialize_sample_scaler(AVCodecContext *codec_ctx, double width, double height)
{
  SwsContext *swsctx = sws_getContext(width, height, AV_PIX_FMT_BGR24, width, height, codec_ctx->pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr);
  if (!swsctx)
  {
    cout << "Could not initialize sample scaler!" << endl;
    exit(1);
  }

  return swsctx;
}

AVFrame *allocate_frame_buffer(AVCodecContext *codec_ctx, double width, double height)
{
  AVFrame *frame = av_frame_alloc();

  vector<uint8_t> framebuf(av_image_get_buffer_size(codec_ctx->pix_fmt, width, height, 1));
  av_image_fill_arrays(frame->data, frame->linesize, framebuf.data(), codec_ctx->pix_fmt, width, height, 1);
  frame->width = width;
  frame->height = height;
  frame->format = static_cast<int>(codec_ctx->pix_fmt);

  return frame;
}

void write_frame(AVCodecContext *codec_ctx, AVFormatContext *fmt_ctx, AVFrame *frame)
{
  AVPacket pkt = {0};
  av_init_packet(&pkt);

  int ret = avcodec_send_frame(codec_ctx, frame);
  if (ret < 0)
  {
    cout << "Error sending frame to codec context!" << endl;
    exit(1);
  }

  ret = avcodec_receive_packet(codec_ctx, &pkt);
  if (ret < 0)
  {
    cout << "Error receiving packet from codec context!" << endl;
    exit(1);
  }

  av_interleaved_write_frame(fmt_ctx, &pkt);
  av_packet_unref(&pkt);
}

void stream_video(double width, double height, int fps, string camID, int bitrate, string codec_profile, string server)
{
	//该函数在所有基于ffmpeg的应用程序中几乎都是第一个被调用的。只有调用了该函数，才能使用复用器，编码器等
	av_register_all();
	//使用ffmpeg类库进行开发的时候，打开流媒体（或本地文件）的函数是avformat_open_input()。  
	//其中打开网络流的话，前面要加上函数avformat_network_init()
	avformat_network_init();

    const char *output = server.c_str();
    int ret;
    auto cam = get_device(camID, width, height);
    vector<uint8_t> imgbuf(height * width * 3 + 16);
    cv::Mat image(height, width, CV_8UC3, imgbuf.data(), width * 3);
    AVFormatContext *ofmt_ctx = nullptr;
    AVCodec *out_codec = nullptr;
    AVStream *out_stream = nullptr;
    AVCodecContext *out_codec_ctx = nullptr;

    initialize_avformat_context(ofmt_ctx, "flv");
    initialize_io_context(ofmt_ctx, output);

    out_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    out_stream = avformat_new_stream(ofmt_ctx, out_codec);
    out_codec_ctx = avcodec_alloc_context3(out_codec);

    set_codec_params(ofmt_ctx, out_codec_ctx, width, height, fps, bitrate);
    initialize_codec_stream(out_stream, out_codec_ctx, out_codec, codec_profile);

    out_stream->codecpar->extradata = out_codec_ctx->extradata;
    out_stream->codecpar->extradata_size = out_codec_ctx->extradata_size;

    av_dump_format(ofmt_ctx, 0, output, 1);

	//摄像头数据往往是RGB格式的，需要将其转换为YUV420P格式，所以要先做如下的准备工作
    auto *swsctx = initialize_sample_scaler(out_codec_ctx, width, height);
    auto *frame = allocate_frame_buffer(out_codec_ctx, width, height);

    int cur_size;
    uint8_t *cur_ptr;

    ret = avformat_write_header(ofmt_ctx, nullptr);
    if (ret < 0)
    {
      cout << "Could not write header!" << endl;
      exit(1);
    }
    else
        cout<<"ret="<<ret<<endl;

    do
    {
      cam >> image;
	  if(image.empty())
		  break;
	  cv::resize(image, image, cv::Size(width, height));
      const int stride[] = {static_cast<int>(image.step[0])};

      sws_scale(swsctx, &image.data, stride, 0, image.rows, frame->data, frame->linesize);
      frame->pts += av_rescale_q(1, out_codec_ctx->time_base, out_stream->time_base);
      write_frame(out_codec_ctx, ofmt_ctx, frame);

    } while (true);

    av_write_trailer(ofmt_ctx);

    if( &frame)
      av_frame_free(&frame);
   // if( out_codec_ctx)
   // avcodec_close(out_codec_ctx);
    if( ofmt_ctx->pb )
      avio_close(ofmt_ctx->pb);
    if( ofmt_ctx)
      avformat_free_context(ofmt_ctx);

    sws_freeContext(swsctx);   
}

int main(int argc, char *argv[])
{
  int fps = 30, width = 800, height = 600, bitrate = 300000;
  string cameraID = "0";
  string h264profile = "high444";
  string outputServer = "rtmp://localhost/live/stream";
  bool dump_log = false;

  auto cli = (
    (option("-c", "--camera") & value("camera", cameraID)) % "camera ID (default: 0)",
    (option("-o", "--output") & value("output", outputServer)) % "output RTMP server (default: rtmp://localhost/live/stream)",
    (option("-f", "--fps") & value("fps", fps)) % "frames-per-second (default: 30)",
    (option("-w", "--width") & value("width", width)) % "video width (default: 800)",
    (option("-h", "--height") & value("height", height)) % "video height (default: 640)",
    (option("-b", "--bitrate") & value("bitrate", bitrate)) % "stream bitrate in kb/s (default: 300000)",
    (option("-p", "--profile") & value("profile", h264profile)) % "H264 codec profile (baseline | high | high10 | high422 | high444 | main) (default: high444)",
    (option("-l", "--log") & value("log", dump_log)) % "print debug output (default: false)"
  );

  if (!parse(argc, argv, cli))
  {
    cout << make_man_page(cli, argv[0]) << endl;
  }

  if (dump_log)
  {
    av_log_set_level(AV_LOG_DEBUG);
  }

  stream_video(width, height, fps, cameraID, bitrate, h264profile, outputServer);

  cout<<"cameraID="<<cameraID<<endl;
  return 0;
}
