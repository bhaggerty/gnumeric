/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * analysis-chi-squared.c:
 *
 * Author:
 *   Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2009 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "analysis-chi-squared.h"
#include "analysis-tools.h"
#include "value.h"
#include "ranges.h"
#include "expr.h"
#include "func.h"
#include "numbers.h"

static gboolean
analysis_tool_chi_squared_engine_run (data_analysis_output_t *dao,
				      analysis_tools_data_chi_squared_t *info)
{
	GnmExpr const *expr_check;
	GnmExpr const *expr_region;
	GnmExpr const *expr_statistic;
	GnmExpr const *expr_ones;
	GnmExpr const *expr_row;
	GnmExpr const *expr_expect;

	GnmFunc *fd_mmult;
	GnmFunc *fd_row;
	GnmFunc *fd_transpose;
	GnmFunc *fd_sum;
	GnmFunc *fd_min;
	GnmFunc *fd_offset;
	GnmFunc *fd_chiinv;
	GnmFunc *fd_chidist;
	char const *label;
	char *cc;

	label = (info->independence)
		? _("[>=5]\"Test of Independence\";[<5][Red]\"Invalid Test of Independence\"")
		: _("[>=5]\"Test of Homogeneity\";[<5][Red]\"Invalid Test of Homogeneity\"");

	fd_mmult = gnm_func_lookup_or_add_placeholder ("MMULT", dao->sheet ? dao->sheet->workbook : NULL, FALSE);
	gnm_func_ref (fd_mmult);
	fd_row = gnm_func_lookup_or_add_placeholder ("ROW", dao->sheet ? dao->sheet->workbook : NULL, FALSE);
	gnm_func_ref (fd_row);
	fd_transpose = gnm_func_lookup_or_add_placeholder ("TRANSPOSE", dao->sheet ? dao->sheet->workbook : NULL, FALSE);
	gnm_func_ref (fd_transpose);
	fd_sum = gnm_func_lookup_or_add_placeholder ("SUM", dao->sheet ? dao->sheet->workbook : NULL, FALSE);
	gnm_func_ref (fd_sum);
	fd_min = gnm_func_lookup_or_add_placeholder ("MIN", dao->sheet ? dao->sheet->workbook : NULL, FALSE);
	gnm_func_ref (fd_min);
	fd_offset = gnm_func_lookup_or_add_placeholder ("OFFSET", dao->sheet ? dao->sheet->workbook : NULL, FALSE);
	gnm_func_ref (fd_offset);
	fd_chiinv = gnm_func_lookup_or_add_placeholder ("CHIINV", dao->sheet ? dao->sheet->workbook : NULL, FALSE);
	gnm_func_ref (fd_chiinv);
	fd_chidist = gnm_func_lookup_or_add_placeholder ("CHIDIST", dao->sheet ? dao->sheet->workbook : NULL, FALSE);
	gnm_func_ref (fd_chidist);

	dao_set_italic (dao, 0, 1, 0, 4);
	set_cell_text_col (dao, 0, 1, _("/Test Statistic:"
					"/Degrees of Freedom:"
					"/p-Value:"
					"/Critical Value:"));
	cc = g_strdup_printf ("%s = %.2" GNM_FORMAT_f, "\xce\xb1", info->alpha);
	dao_set_cell_comment (dao, 0, 4, cc);
	g_free (cc);

	if (info->labels)
		expr_region = gnm_expr_new_funcall5
			(fd_offset,
			 gnm_expr_new_constant (value_dup (info->input)),
			 gnm_expr_new_constant (value_new_int (1)),
			 gnm_expr_new_constant (value_new_int (1)),
			 gnm_expr_new_constant (value_new_int (info->n_r)),
			 gnm_expr_new_constant (value_new_int (info->n_c)));
	else
		expr_region = gnm_expr_new_constant (value_dup (info->input));

	expr_row = gnm_expr_new_funcall1 (fd_row, gnm_expr_copy (expr_region));
	expr_ones = gnm_expr_new_binary (gnm_expr_copy (expr_row),
					 GNM_EXPR_OP_DIV,
					 expr_row);
	expr_expect = gnm_expr_new_binary (gnm_expr_new_funcall2
					   (fd_mmult,
					    gnm_expr_new_funcall2
					    (fd_mmult,
					     gnm_expr_copy (expr_region),
					     gnm_expr_copy (expr_ones)),
					    gnm_expr_new_funcall2
					    (fd_mmult,
					     gnm_expr_new_funcall1 (fd_transpose, gnm_expr_copy (expr_ones)),
					     gnm_expr_copy (expr_region))),
					   GNM_EXPR_OP_DIV,
					   gnm_expr_new_funcall1 (fd_sum, gnm_expr_copy (expr_region)));

	expr_check = gnm_expr_new_funcall1 (fd_min, gnm_expr_copy (expr_expect));
	dao_set_merge (dao, 0, 0, 1, 0);
	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell_expr (dao, 0, 0, expr_check);
	dao_set_format (dao, 0, 0, 0, 0, label);
	dao_set_align (dao, 0, 0, 0, 0, HALIGN_CENTER, VALIGN_BOTTOM);

	expr_statistic = gnm_expr_new_funcall1 (fd_sum,
						gnm_expr_new_binary
						(gnm_expr_new_binary (gnm_expr_new_binary
								      (gnm_expr_copy (expr_region),
								       GNM_EXPR_OP_SUB,
								       gnm_expr_copy (expr_expect)),
								      GNM_EXPR_OP_EXP,
								      gnm_expr_new_constant (value_new_int (2))),
						 GNM_EXPR_OP_DIV,
						 gnm_expr_copy (expr_expect)));
	dao_set_cell_array_expr (dao, 1, 1, expr_statistic);

	dao_set_cell_int (dao, 1, 2, (info->n_r - 1)*(info->n_c - 1));
	dao_set_cell_expr(dao, 1, 3, gnm_expr_new_funcall2
			  (fd_chidist, make_cellref (0,-2),  make_cellref (0,-1)));
	dao_set_cell_expr(dao, 1, 4, gnm_expr_new_funcall2
			  (fd_chiinv,
			   gnm_expr_new_constant (value_new_float (info->alpha)),
			   make_cellref (0,-2)));

	gnm_func_unref (fd_mmult);
	gnm_func_unref (fd_row);
	gnm_func_unref (fd_transpose);
	gnm_func_unref (fd_sum);
	gnm_func_unref (fd_min);
	gnm_func_unref (fd_offset);
	gnm_func_unref (fd_chiinv);
	gnm_func_unref (fd_chidist);

	gnm_expr_free (expr_ones);
	gnm_expr_free (expr_expect);
	gnm_expr_free (expr_region);
	dao_redraw_respan (dao);

	return FALSE;
}

static gboolean
analysis_tool_chi_squared_clean (gpointer specs)
{
	analysis_tools_data_chi_squared_t *info = specs;

	value_release (info->input);
	info->input = NULL;

	return FALSE;
}


gboolean
analysis_tool_chi_squared_engine (data_analysis_output_t *dao, gpointer specs,
			      analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_chi_squared_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor
			(dao,
			 info->independence ?
			 _("Test of Independence (%s)")
			 : _("Test of Homogeneity (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		dao_adjust (dao, 2, 5);
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_chi_squared_clean (specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, info->independence ?
				    _("Test of Independence")
				    : _("Test of Homogeneity"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao,  info->independence ?
					  _("Test of Independence")
					  : _("Test of Homogeneity"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_chi_squared_engine_run (dao, specs);
	}
	return TRUE;
}




