// Copyright (C) 2006  Davis E. King (davis@dlib.net)
// License: Boost Software License   See LICENSE.txt for the full license.
#undef DLIB_EQUALIZE_HISTOGRAm_ABSTRACT_
#ifdef DLIB_EQUALIZE_HISTOGRAm_ABSTRACT_

#include "../pixel.h"
#include "../matrix.h"

namespace dlib
{

// ---------------------------------------------------------------------------------------

    template <
        typename in_image_type,
        typename out_image_type 
        >
    void equalize_histogram (
        const in_image_type& in_img,
        out_image_type& out_img
    );
    /*!
        requires
            - in_image_type == is an implementation of array2d/array2d_kernel_abstract.h
            - out_image_type == is an implementation of array2d/array2d_kernel_abstract.h
            - pixel_traits<typename in_image_type::type>::has_alpha == false
            - pixel_traits<typename out_image_type::type>::has_alpha == false 
        ensures
            - #out_img == the histogram equalized version of in_img
            - #out_img.nc() == in_img.nc()
            - #out_img.nr() == in_img.nr()
    !*/

// ---------------------------------------------------------------------------------------

    template <
        typename in_image_type,
        long R,
        long C,
        typename MM
        >
    void get_histogram (
        const in_image_type& in_img,
        matrix<unsigned long,R,C,MM>& hist
    );
    /*!
        requires
            - in_image_type == is an implementation of array2d/array2d_kernel_abstract.h
            - hist must be capable of representing a column vector of length 
              pixel_traits<typename in_image_type>::max(). I.e. if R and C are nonzero
              then they must be values that don't conflict with the previous sentence.
        ensures
            - #hist.size() == pixel_traits<typename in_image_type>::max()
            - #hist.nc() == 1 || #hist.nr() == 1 (i.e. hist is either a row or column vector)
            - #hist == the histogram for in_img.  I.e. it is the case that for all
              valid i:
                - hist(i) == the number of times a pixel with intensity i appears
                  in in_img
    !*/

// ---------------------------------------------------------------------------------------

}

#endif // DLIB_EQUALIZE_HISTOGRAm_ABSTRACT_


