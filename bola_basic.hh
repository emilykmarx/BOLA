#ifndef BOLA_BASIC_HH
#define BOLA_BASIC_HH

#include <vector>
#include <fstream>
#include "abr_algo.hh"

/* BOLA-BASIC, with V and gamma set as suggested in section IV.B */

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

        /* Write data for figures every PLOT_INTERVAL slots? Maybe every slot during startup */
        // static constexpr unsigned PLOT_INTERVAL = 5;

        /* Minimum buffer level, in chunks (3 as recommended by paper).
         * "Minimum" is the threshold where we switch from smallest chunk 
         * (TODO: or worst quality?) to next smallest. */
        static constexpr unsigned MIN_BUF_CHUNKS = 3;
       
        double utility(double raw_ssim) const;
       
        double raw_ssim_to_db(double raw_ssim) const;
       
        double objective(BolaBasic::Parameters params, const Encoded& encoded, double client_buf_chunks) const;
        
        std::optional<Parameters> calculate_parameters(double min_buf_chunks, double max_buf_chunks,
                                        std::vector<Encoded> & encoded_formats) const;
        
        VideoFormat choose_max_objective(const Parameters & params, 
                                         const std::vector<Encoded> & encoded_formats, 
                                         double client_buf_chunks) const;
        
        /* Logging */
        void do_logging(uint64_t vts, const Parameters & params); 

        void fig_1(const std::vector<Encoded> & encoded_formats, const Parameters & params,
                   double chunk_duration_s, uint64_t vts) const;

        void fig_2(const std::vector<Encoded> & encoded_formats, const Parameters & params,
                   double chunk_duration_s, uint64_t vts) const;
    
        const std::string log_filename = "BolaBasic_log.txt";
        std::fstream log{};
    
    private:
        // TODO: move stuff to private after test
};

#endif /* BOLA_BASIC_HH */
