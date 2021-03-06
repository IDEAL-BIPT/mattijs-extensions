/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2017 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

Class
    Foam::unallocatedGenericFvPatch

Description
    Version of genericFvPatch without storage

SourceFiles
    unallocatedGenericFvPatch.C

\*---------------------------------------------------------------------------*/

#ifndef unallocatedGenericFvPatch_H
#define unallocatedGenericFvPatch_H

#include "fvPatch.H"
#include "genericPolyPatch.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

/*---------------------------------------------------------------------------*\
                  Class unallocatedGenericFvPatch Declaration
\*---------------------------------------------------------------------------*/

class unallocatedGenericFvPatch
:
    public fvPatch
{
    const word name_;

    const label size_;

public:

    //- Runtime type information
    //TypeName(genericPolyPatch::typeName_());
    TypeName("unallocatedGeneric");


    // Constructors

        //- Construct from components
        unallocatedGenericFvPatch
        (
            const polyPatch& patch,
            const word& name,
            const label size,
            const fvBoundaryMesh& bm
        )
        :
            fvPatch(patch, bm),
            name_(name),
            size_(size)
        {}


     //- Destructor
     virtual ~unallocatedGenericFvPatch()
    {}


    // Member Functions

            //- Return size
            label size() const
            {
                return size_;
            }

            //- Return name
            const word& name() const
            {
                return name_;
            }
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
