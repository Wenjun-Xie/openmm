/* -------------------------------------------------------------------------- *
 *                                   OpenMM                                   *
 * -------------------------------------------------------------------------- *
 * This is part of the OpenMM molecular simulation toolkit originating from   *
 * Simbios, the NIH National Center for Physics-Based Simulation of           *
 * Biological Structures at Stanford, funded under the NIH Roadmap for        *
 * Medical Research, grant U54 GM072970. See https://simtk.org.               *
 *                                                                            *
 * Portions copyright (c) 2019 Stanford University and the Authors.           *
 * Authors: Andreas Krämer and Andrew C. Simmonett                            *
 * Contributors:                                                              *
 *                                                                            *
 * Permission is hereby granted, free of charge, to any person obtaining a    *
 * copy of this software and associated documentation files (the "Software"), *
 * to deal in the Software without restriction, including without limitation  *
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,   *
 * and/or sell copies of the Software, and to permit persons to whom the      *
 * Software is furnished to do so, subject to the following conditions:       *
 *                                                                            *
 * The above copyright notice and this permission notice shall be included in *
 * all copies or substantial portions of the Software.                        *
 *                                                                            *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   *
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    *
 * THE AUTHORS, CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,    *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR      *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE  *
 * USE OR OTHER DEALINGS IN THE SOFTWARE.                                     *
 * -------------------------------------------------------------------------- */

#include "openmm/VelocityVerletIntegrator.h"
#include "openmm/Context.h"
#include "openmm/Force.h"
#include "openmm/System.h"
#include "openmm/NoseHooverChain.h"
#include "openmm/OpenMMException.h"
#include "openmm/CMMotionRemover.h"
#include "openmm/internal/ContextImpl.h"
#include "openmm/kernels.h"
#include <string>

using namespace OpenMM;
using std::string;
using std::vector;

VelocityVerletIntegrator::VelocityVerletIntegrator(double stepSize) {
    setStepSize(stepSize);
    setConstraintTolerance(1e-5);
}

double VelocityVerletIntegrator::propagateChain(double kineticEnergy, int chainID) {
}

/*int VelocityVerletIntegrator::addMaskedNoseHooverChain(System& system, std::vector<int> mask, std::vector<int> parents, double temperature,
                                                 double frequency, int numDOFs, int numMTS, int numYoshidaSuzuki){

}*/

int VelocityVerletIntegrator::addNoseHooverChainThermostat(System& system, double temperature, double collisionFrequency,
                                                           int chainLength, int numMTS, int numYoshidaSuzuki) {
    std::vector<int> mask, parents;
    int nDOF = 0;
    int numForces = system.getNumForces();
    for(int particle = 0; particle < system.getNumParticles(); ++particle) {
        if(system.getParticleMass(particle) > 0) nDOF += 3;
    }
    nDOF -= system.getNumConstraints();
    for (int forceNum = 0; forceNum < numForces; ++forceNum) {
        if (dynamic_cast<CMMotionRemover*>(&system.getForce(forceNum))) nDOF -= 3;
    }

    auto nhcForce = new NoseHooverChain(temperature, collisionFrequency, nDOF, chainLength,
                                        numMTS, numYoshidaSuzuki, std::to_string(noseHooverChains.size()));
    system.addForce(nhcForce);
    noseHooverChains.push_back(nhcForce);
}

double VelocityVerletIntegrator::computeKineticEnergy() {
    return vvKernel.getAs<IntegrateVelocityVerletStepKernel>().computeKineticEnergy(*context, *this);
}

void VelocityVerletIntegrator::initialize(ContextImpl& contextRef) {
    if (owner != NULL && &contextRef.getOwner() != owner)
        throw OpenMMException("This Integrator is already bound to a context");
    context = &contextRef;
    owner = &contextRef.getOwner();
    vvKernel = context->getPlatform().createKernel(IntegrateVelocityVerletStepKernel::Name(), contextRef);
    vvKernel.getAs<IntegrateVelocityVerletStepKernel>().initialize(contextRef.getSystem(), *this);
    for (auto * nhc: noseHooverChains){
        nhcKernels.push_back(context->getPlatform().createKernel(PropagateNoseHooverChainKernel::Name(), contextRef));
        nhcKernels.back().getAs<PropagateNoseHooverChainKernel>().initialize(contextRef.getSystem(), *nhc);
    }
}

void VelocityVerletIntegrator::cleanup() {
    vvKernel = Kernel();
    nhcKernels.clear();
}

vector<string> VelocityVerletIntegrator::getKernelNames() {
    std::vector<std::string> names;
    names.push_back(PropagateNoseHooverChainKernel::Name());
    names.push_back(IntegrateVelocityVerletStepKernel::Name());
    return names;
}

void VelocityVerletIntegrator::step(int steps) {
    if (context == NULL)
        throw OpenMMException("This Integrator is not bound to a context!");
    for (int i = 0; i < steps; ++i) {
        context->updateContextState();
        vvKernel.getAs<IntegrateVelocityVerletStepKernel>().execute(*context, *this);
    }
}
