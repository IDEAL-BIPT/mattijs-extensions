/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2011-2016 OpenFOAM Foundation
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

\*---------------------------------------------------------------------------*/

#include "pairGAMGAgglomeration.H"
#include "lduAddressing.H"

// * * * * * * * * * * * * * Protected Member Functions  * * * * * * * * * * //

void Foam::pairGAMGAgglomeration::agglomerate
(
    const lduMesh& mesh,
    const scalarField& faceWeights
)
{
    // Start geometric agglomeration from the given faceWeights
    scalarField* faceWeightsPtr = const_cast<scalarField*>(&faceWeights);

    // Agglomerate until the required number of cells in the coarsest level
    // is reached

    label nPairLevels = 0;
    label nCreatedLevels = 0;

    while (nCreatedLevels < maxLevels_ - 1)
    {
        label nCoarseCells = -1;

        tmp<labelField> finalAgglomPtr = agglomerate
        (
            nCoarseCells,
            meshLevel(nCreatedLevels).lduAddr(),
            *faceWeightsPtr
        );

        if (continueAgglomerating(nCoarseCells))
        {
            nCells_[nCreatedLevels] = nCoarseCells;
            restrictAddressing_.set(nCreatedLevels, finalAgglomPtr);
        }
        else
        {
            break;
        }

        agglomerateLduAddressing(nCreatedLevels);

        // Agglomerate the faceWeights field for the next level
        {
            scalarField* aggFaceWeightsPtr
            (
                new scalarField
                (
                    meshLevels_[nCreatedLevels].upperAddr().size(),
                    0.0
                )
            );

            restrictFaceField
            (
                *aggFaceWeightsPtr,
                *faceWeightsPtr,
                nCreatedLevels
            );

            if (nCreatedLevels)
            {
                delete faceWeightsPtr;
            }

            faceWeightsPtr = aggFaceWeightsPtr;
        }

        if (nPairLevels % mergeLevels_)
        {
            combineLevels(nCreatedLevels);
        }
        else
        {
            nCreatedLevels++;
        }

        nPairLevels++;
    }

    // Shrink the storage of the levels to those created
    compactLevels(nCreatedLevels);

    // Delete temporary geometry storage
    if (nCreatedLevels)
    {
        delete faceWeightsPtr;
    }
}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

Foam::tmp<Foam::labelField> Foam::pairGAMGAgglomeration::agglomerate
(
    label& nCoarseCells,
    const lduAddressing& fineMatrixAddressing,
    const scalarField& faceWeights
)
{
    const label nFineCells = fineMatrixAddressing.size();

    const labelUList& upperAddr = fineMatrixAddressing.upperAddr();
    const labelUList& lowerAddr = fineMatrixAddressing.lowerAddr();

Pout<< "agglomerate at nCells:" << nFineCells
    << " band():" << fineMatrixAddressing.band() << endl;


    // For each cell calculate faces
    labelList cellFaces(upperAddr.size() + lowerAddr.size());
    labelList cellFaceOffsets(nFineCells + 1);

    // memory management
    {
        labelList nNbrs(nFineCells, 0);

        forAll(upperAddr, facei)
        {
            nNbrs[upperAddr[facei]]++;
        }

        forAll(lowerAddr, facei)
        {
            nNbrs[lowerAddr[facei]]++;
        }

        cellFaceOffsets[0] = 0;
        forAll(nNbrs, celli)
        {
            cellFaceOffsets[celli+1] = cellFaceOffsets[celli] + nNbrs[celli];
        }

        // reset the whole list to use as counter
        nNbrs = 0;

        forAll(upperAddr, facei)
        {
            cellFaces
            [
                cellFaceOffsets[upperAddr[facei]] + nNbrs[upperAddr[facei]]
            ] = facei;

            nNbrs[upperAddr[facei]]++;
        }

        forAll(lowerAddr, facei)
        {
            cellFaces
            [
                cellFaceOffsets[lowerAddr[facei]] + nNbrs[lowerAddr[facei]]
            ] = facei;

            nNbrs[lowerAddr[facei]]++;
        }
    }

    // Determine cell visit order. This will lower the effect of having
    // one cell clustering with a neighbouring one whereas it would be better
    // if that neighbour clustered with another one of its neighbours. So
    // instead make sure to start with the cells with highest face weights first

    labelList visitOrder(nFineCells);
    {
        scalarField maxCellWeight(nFineCells, -GREAT);
        forAll(upperAddr, facei)
        {
            scalar& cWeight = maxCellWeight[upperAddr[facei]];
            cWeight = max(cWeight, faceWeights[facei]);
        }
        forAll(lowerAddr, facei)
        {
            scalar& cWeight = maxCellWeight[lowerAddr[facei]];
            cWeight = max(cWeight, faceWeights[facei]);
        }

        sortedOrder
        (
            maxCellWeight,
            visitOrder,
            typename UList<scalar>::greater(maxCellWeight)
        );
    }


    // Go through the faces and create clusters

    tmp<labelField> tcoarseCellMap(new labelField(nFineCells, -1));
    labelField& coarseCellMap = tcoarseCellMap.ref();

    nCoarseCells = 0;

    for (label cellfi=0; cellfi<nFineCells; cellfi++)
    {
        // Change cell ordering depending on direction for this level
        //label celli = forward_ ? cellfi : nFineCells - cellfi - 1;
        //label celli = cellfi;
        label celli = visitOrder[cellfi];

        if (coarseCellMap[celli] < 0)
        {
            label matchFaceNo = -1;
            scalar maxFaceWeight = -GREAT;

            // check faces to find ungrouped neighbour with largest face weight
            for
            (
                label faceOs=cellFaceOffsets[celli];
                faceOs<cellFaceOffsets[celli+1];
                faceOs++
            )
            {
                label facei = cellFaces[faceOs];

                // I don't know whether the current cell is owner or neighbour.
                // Therefore I'll check both sides
                if
                (
                    coarseCellMap[upperAddr[facei]] < 0
                 && coarseCellMap[lowerAddr[facei]] < 0
                 && faceWeights[facei] > maxFaceWeight
                )
                {
                    // Match found. Pick up all the necessary data
                    matchFaceNo = facei;
                    maxFaceWeight = faceWeights[facei];
                }
            }

            if (matchFaceNo >= 0)
            {
                // Make a new group
                coarseCellMap[upperAddr[matchFaceNo]] = nCoarseCells;
                coarseCellMap[lowerAddr[matchFaceNo]] = nCoarseCells;
                nCoarseCells++;
            }
            else
            {
                // No match. Find the best neighbouring cluster and
                // put the cell there
                label clusterMatchFaceNo = -1;
                scalar clusterMaxFaceCoeff = -GREAT;

                for
                (
                    label faceOs=cellFaceOffsets[celli];
                    faceOs<cellFaceOffsets[celli+1];
                    faceOs++
                )
                {
                    label facei = cellFaces[faceOs];

                    if (faceWeights[facei] > clusterMaxFaceCoeff)
                    {
                        clusterMatchFaceNo = facei;
                        clusterMaxFaceCoeff = faceWeights[facei];
                    }
                }

                if (clusterMatchFaceNo >= 0)
                {
                    // Add the cell to the best cluster
                    coarseCellMap[celli] = max
                    (
                        coarseCellMap[upperAddr[clusterMatchFaceNo]],
                        coarseCellMap[lowerAddr[clusterMatchFaceNo]]
                    );
                }
            }
        }
    }

    // Check that all cells are part of clusters,
    // if not create single-cell "clusters" for each
    for (label cellfi=0; cellfi<nFineCells; cellfi++)
    {
        // Change cell ordering depending on direction for this level
        //label celli = forward_ ? cellfi : nFineCells - cellfi - 1;
        //label celli = cellfi;
        label celli = visitOrder[cellfi];

        if (coarseCellMap[celli] < 0)
        {
            coarseCellMap[celli] = nCoarseCells;
            nCoarseCells++;
        }
    }


    // Have renumbering (below) start at different cells. Not sure whether
    // this is needed with renumbering
    if (!forward_)
    {
        forAll(coarseCellMap, celli)
        {
            coarseCellMap[celli] = nCoarseCells - 1 - coarseCellMap[celli];
        }
    }


    // Renumber coarse level
    {
        // Construct cell-cell addressing on coarse level

        // Pass 1: count
        labelList nNbrs(nCoarseCells, 0);

        forAll(upperAddr, facei)
        {
            label coarseUpper = coarseCellMap[upperAddr[facei]];
            label coarseLower = coarseCellMap[lowerAddr[facei]];

            if (coarseUpper != coarseLower)
            {
                nNbrs[coarseUpper]++;
                nNbrs[coarseLower]++;
            }
        }

        // Size
        labelListList cellCells(nCoarseCells);
        forAll(cellCells, celli)
        {
            cellCells[celli].setSize(nNbrs[celli]);
        }

        // Pass2: fill
        nNbrs = 0;

        forAll(upperAddr, facei)
        {
            label coarseUpper = coarseCellMap[upperAddr[facei]];
            label coarseLower = coarseCellMap[lowerAddr[facei]];

            if (coarseUpper != coarseLower)
            {
                {
                    labelList& upper = cellCells[coarseUpper];
                    SubList<label> used(upper, nNbrs[coarseUpper]);

                    if (findIndex(used, coarseLower) == -1)
                    {
                        upper[nNbrs[coarseUpper]++] = coarseLower;
                    }
                }
                {
                    labelList& lower = cellCells[coarseLower];
                    SubList<label> used(lower, nNbrs[coarseLower]);

                    if (findIndex(used, coarseUpper) == -1)
                    {
                        lower[nNbrs[coarseLower]++] = coarseUpper;
                    }
                }
            }
        }
        forAll(cellCells, celli)
        {
            cellCells[celli].setSize(nNbrs[celli]);
        }


        // Renumber (CutHill-McKee) and apply to cell map
        labelList newToOld(bandCompression(cellCells));
        labelList oldToNew(invert(newToOld.size(), newToOld));
        coarseCellMap = UIndirectList<label>(oldToNew, coarseCellMap)();
    }


    // Reverse the map ordering for the next level
    // to improve the next level of agglomeration
    forward_ = !forward_;

    return tcoarseCellMap;
}


// ************************************************************************* //
