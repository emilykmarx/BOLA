#ifndef BOLA_BASIC_HH
#define BOLA_BASIC_HH

#include <vector>
#include <array>
#include <fstream>
#include <string>
#include "abr_algo.hh"
#include "ws_client.hh"

/* BOLA-BASIC, with V and gamma set statically based on min/max buffer level. */

class BolaBasic : public ABRAlgo
{
    public:
        BolaBasic(const WebSocketClient & client,
                  const std::string & abr_name, const YAML::Node & abr_config);

        VideoFormat select_video_format() override;
    
        struct Parameters {
            double V;
            double gp;
        };
       
        struct Encoded {
            VideoFormat vf;
            size_t size;    // bytes (as in vdata map) 
            double utility;
        };

        Parameters params{};

        static constexpr unsigned NFORMATS = 10;

        // Must be ordered nondecreasing wrt size/utility (TODO assert this, also size0 < size1, min < max)
        // TODO: make const after test
        // LEFT OFF: do TODOs, do test w/ static ladder, then send to Francis
        static constexpr std::array<size_t, NFORMATS> size_ladder_bytes = 
            { 44319, 93355, 115601, 142904, 196884, 263965, 353752, 494902, 632193, 889893 };
        
        static constexpr std::array<double, NFORMATS> ssim_index_ladder = 
            { 0.91050748,  0.94062527,  0.94806355,  0.95498943,  0.96214503,
            0.96717277,  0.97273958,  0.97689813,  0.98004106,  0.98332605 };
        
        /* Number of Fig1/Fig2 to make (starting at vts 0) */
        // static constexpr unsigned NPLOTS = 5;

        /* Write data for figures every PLOT_INTERVAL slots? Maybe every slot during startup */
        // static constexpr unsigned PLOT_INTERVAL = 5;

        /* Minimum buffer level, in  seconds.
         * "Minimum" is the threshold where we switch from smallest chunk 
         * (TODO: or worst quality?) to next smallest. */
        double MIN_BUF_S = 3;
        // TODO: change V and gp calculations to use sec, not chunks
        double DEFAULT_CHUNK_DURATION_S = 2.002;
        /* TODO: move these to Channel.hh if needed
            Channel::DEFAULT_VIDEO_DURATION * 1.0 / Channel::DEFAULT_TIMESCALE;
         */
        double MIN_BUF_CHUNKS = MIN_BUF_S / DEFAULT_CHUNK_DURATION_S;
        double MAX_BUF_CHUNKS = WebSocketClient::MAX_BUFFER_S / DEFAULT_CHUNK_DURATION_S;
       
        double utility(double raw_ssim) const;
       
        double raw_ssim_to_db(double raw_ssim) const;
       
        double objective(const Encoded& encoded, double client_buf_chunks) const;
        
        void calculate_parameters(std::optional<std::vector<Encoded>> encoded_formats);
        
        Encoded choose_max_objective(const std::vector<Encoded> & encoded_formats, 
                                         double client_buf_chunks) const;
        
        /* Logging */
        void do_logging(const std::vector<Encoded> & encoded_formats, 
                           double chunk_duration_s, uint64_t vts, const std::string & channel_name);

        void fig_1(const std::vector<Encoded> & encoded_formats, 
                           double chunk_duration_s, uint64_t vts, const std::string & channel_name) const;

        void fig_2(const std::vector<Encoded> & encoded_formats,
                           double chunk_duration_s, uint64_t vts, const std::string & channel_name) const;
    
        const std::string log_filename = "abr/test/log.txt";
        std::fstream log{};
    
    private:
        // TODO: move stuff to private after test
};

#endif /* BOLA_BASIC_HH */
