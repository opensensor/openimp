#include "common.h"

void h264e_sps_write(bs_t *s, h264_sps_t *sps)
{
	bs_realign(s);
	bs_write(s, 8, sps->i_profile_idc);

	bs_write1(s, sps->b_constraint_set0);
	bs_write1(s, sps->b_constraint_set1);
	bs_write1(s, sps->b_constraint_set2);
	bs_write1(s, sps->b_constraint_set3);

	bs_write(s, 4, 0); //reserved_zero_4bits; /* equal to 0*/
	bs_write(s, 8, sps->i_level_idc);
	bs_write_ue(s, sps->i_id);

	if(sps->i_profile_idc >= PROFILE_HIGH) {
		bs_write_ue(s, sps->i_chroma_format_idc);

		if(sps->i_chroma_format_idc == CHROMA_444)
			bs_write1(s, 0);

		bs_write_ue(s, 0); // bit_depth_luma_minus8;
		bs_write_ue(s, 0); // bit_depth_chroma_minus8;

		bs_write1(s, sps->b_qpprime_y_zero_transform_bypass);
		/* The T30 path uses the default scaling matrices. */
		bs_write1(s, 0); // seq_scaling_matrix_present_flag
	}

	bs_write_ue(s, sps->i_log2_max_frame_num - 4); // log2_max_frame_num_minus4;
	bs_write_ue(s, sps->i_poc_type); // pic_order_cnt_type;
	if(sps->i_poc_type == 0)
		bs_write_ue(s, sps->i_log2_max_poc_lsb - 4);

	bs_write_ue( s, sps->i_num_ref_frames );
	bs_write1( s, sps->b_gaps_in_frame_num_value_allowed );
	bs_write_ue( s, sps->i_mb_width - 1 ); /*pic_width_in_mbs_minus1*/
	bs_write_ue( s, (sps->i_mb_height >> !sps->b_frame_mbs_only) - 1);
	bs_write1( s, sps->b_frame_mbs_only );

	if( !sps->b_frame_mbs_only )
		bs_write1( s, sps->b_mb_adaptive_frame_field );
	bs_write1( s, sps->b_direct8x8_inference );

	bs_write1( s, sps->b_crop );
	if( sps->b_crop )
	{
		int h_shift = sps->i_chroma_format_idc == CHROMA_420 || sps->i_chroma_format_idc == CHROMA_422;
		int v_shift = sps->i_chroma_format_idc == CHROMA_420;
		bs_write_ue( s, sps->crop.i_left   >> h_shift );
		bs_write_ue( s, sps->crop.i_right  >> h_shift );
		bs_write_ue( s, sps->crop.i_top    >> v_shift );
		bs_write_ue( s, sps->crop.i_bottom >> v_shift );
	}

	bs_write1( s, sps->b_vui );
	if(sps->b_vui) {
		bs_write1(s, sps->vui.b_aspect_ratio_info_present);
		if(sps->vui.b_aspect_ratio_info_present) {
			bs_write(s, 8, 255); /* Extended_SAR */
			bs_write(s, 16, sps->vui.i_sar_width);
			bs_write(s, 16, sps->vui.i_sar_height);
		}
		bs_write1(s, sps->vui.b_overscan_info_present);
		if(sps->vui.b_overscan_info_present)
			bs_write1(s, sps->vui.b_overscan_info);
		bs_write1(s, sps->vui.b_signal_type_present);
		if(sps->vui.b_signal_type_present) {
			bs_write(s, 3, sps->vui.i_vidformat);
			bs_write1(s, sps->vui.b_fullrange);
			bs_write1(s, sps->vui.b_color_description_present);
			if(sps->vui.b_color_description_present) {
				bs_write(s, 8, sps->vui.i_colorprim);
				bs_write(s, 8, sps->vui.i_transfer);
				bs_write(s, 8, sps->vui.i_colmatrix);
			}
		}
		bs_write1(s, sps->vui.b_chroma_loc_info_present);
		if(sps->vui.b_chroma_loc_info_present) {
			bs_write_ue(s, sps->vui.i_chroma_loc_top);
			bs_write_ue(s, sps->vui.i_chroma_loc_bottom);
		}
		bs_write1(s, sps->vui.b_timing_info_present);
		if(sps->vui.b_timing_info_present) {
			bs_write32(s, sps->vui.i_num_units_in_tick);
			bs_write32(s, sps->vui.i_time_scale);
			bs_write1(s, sps->vui.b_fixed_frame_rate);
		}
		/* This compact Helix writer does not configure HRD buffering. */
		bs_write1(s, 0);
		bs_write1(s, 0);
		bs_write1(s, sps->vui.b_pic_struct_present);
		bs_write1(s, sps->vui.b_bitstream_restriction);
		if(sps->vui.b_bitstream_restriction) {
			bs_write1(s, sps->vui.b_motion_vectors_over_pic_boundaries);
			bs_write_ue(s, sps->vui.i_max_bytes_per_pic_denom);
			bs_write_ue(s, sps->vui.i_max_bits_per_mb_denom);
			bs_write_ue(s, sps->vui.i_log2_max_mv_length_horizontal);
			bs_write_ue(s, sps->vui.i_log2_max_mv_length_vertical);
			bs_write_ue(s, sps->vui.i_num_reorder_frames);
			bs_write_ue(s, sps->vui.i_max_dec_frame_buffering);
		}
	}

	bs_rbsp_trailing( s );
	bs_flush( s );
}

void h264e_pps_write(bs_t *s, h264_sps_t *sps, h264_pps_t *pps)
{
	(void)sps;
	bs_realign(s);
	bs_write_ue(s, pps->i_id);
	bs_write_ue(s, pps->i_sps_id);

	bs_write1(s, pps->b_cabac);
	bs_write1(s, pps->b_pic_order);
	bs_write_ue(s, pps->i_num_slice_groups - 1);

	bs_write_ue(s, pps->i_num_ref_idx_l0_default_active - 1);
	bs_write_ue(s, pps->i_num_ref_idx_l1_default_active - 1);
	bs_write1(s, pps->b_weighted_pred);
	bs_write(s, 2, pps->b_weighted_bipred);

	bs_write_se(s, pps->i_pic_init_qp - 26);
	bs_write_se(s, pps->i_pic_init_qs - 26);
	bs_write_se(s, pps->i_chroma_qp_index_offset);

	bs_write1(s, pps->b_deblocking_filter_control);
	bs_write1(s, pps->b_constrained_intra_pred);
	bs_write1(s, pps->b_redundant_pic_cnt);

	if(pps->b_transform_8x8_mode) {

		bs_write1(s, pps->b_transform_8x8_mode);
		bs_write1(s, 0);

		bs_write_se(s, pps->i_chroma_qp_index_offset);

	}

	bs_rbsp_trailing(s);
	bs_flush(s);
}


void h264e_sei_write(bs_t *s, uint8_t *payload, int payload_size, int payload_type)
{
	int i;
	bs_realign( s );
	for( i = 0; i <= payload_type-255; i += 255 )
		bs_write( s, 8, 255 );
	bs_write( s, 8, payload_type-i );

	for( i = 0; i <= payload_size-255; i += 255 )
		bs_write( s, 8, 255 );
	bs_write( s, 8, payload_size-i );

	for( i = 0; i < payload_size; i++ )
		bs_write( s, 8, payload[i] );

	bs_rbsp_trailing( s );
	bs_flush( s );
}

int h264e_sei_version_write(bs_t *s)
{
	(void)s;
	return 0;
}
