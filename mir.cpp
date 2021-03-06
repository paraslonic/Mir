#include "mir.h"
#include "stdio.h"
#include "string.h"
#include <algorithm>
#include <fstream>
#include <math.h>
#include <time.h>
#include <random>
#include <sstream>
#include <chrono>

using namespace lite;

// consts
int LOG_FREQ = 1000;
int LOG_GENEDIST_FREQ = 1000;
float Mir::maxSubstance = 1000;
float Org::maxEAT = 0.6;
float Org::maxEnergy = 1000;
int Org::maxAge = 3000;
const char Mir::alphabet[4] = {'A','T','G','C'};
int Mir::maxId = 0;

//////////////////////////////////////////////////////////////////////////
// Soul

Soul::Soul()
{
	parent = NULL;
	alive = true;
}

void Soul::die()
{
	alive = false;
	maybeDelete(); // will free memory if possible
}

bool Soul::anyLivingChild()
{
	for(auto c : children)
	{
		if(c->alive || c->anyLivingChild())
		{
			return(true);
		}
	}
	return false;
}

void Soul::maybeDelete()
{
	///cout << "maybeDelete\t"<< name<<"\n";
	if(alive || anyLivingChild() ) return;
	if(parent != NULL)
	{
		auto me = find(parent->children.begin(), parent->children.end(), this);
		if(me != parent->children.end())	parent->children.erase(me);
	}
	if(parent != NULL) parent->maybeDelete();
	//cout << name << " making suicide\n";

	if((int)children.size() > 0)	cout << "NB: deleting with non empty childrens!!!\n";
	delete this;
}

void Soul::deleteAll()
{
	for(auto c : children) c->deleteAll();
	delete this;
}

// Org

int Org::maxId = 0;

Org::Org(Mir* mir)
{
	this->mir = mir;
	id = to_string (maxId++) + "_" + to_string(mir->age); // remove?
	age = 0;
	energy = 0;
	SNPrate = 0;
	if(mir->bSoulLog) soul = new Soul; // should be deleted outsid (not in destructor
}

Org::~Org()
{
}

Org* Org::divide()
{
	Org* newbie = new Org(mir);
	newbie->genome = genome;
	newbie->SNPrate = SNPrate;
	energy /= 2;
	newbie->energy = energy;
	// make genome
	int sumLength = 0;
	for(int g = 0; g < newbie->genome.size(); g++) sumLength += newbie->genome[g].seq.size();

	float meanSNPs = sumLength*newbie->SNPrate;
	unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
	std::mt19937 generator(seed);
	std::poisson_distribution<int> distribution(meanSNPs);
	int SNPcount = distribution(generator);
	for(int i = 0; i < SNPcount; i++)
	{
		int g = rand()%newbie->genome.size();
		int pos = rand()%newbie->genome[g].seq.size();
		newbie->genome[g].seq[pos] = Mir::alphabet[rand()%Mir::alphabetLength];
	}
	// soul things
	if(mir->bSoulLog)
	{
		newbie->soul->parent = this->soul;
		soul->children.push_back(newbie->soul);
	}
	return newbie;
}

float Org::meanFit()
{
	float fit = 0;
	for(int g = 0; g < genome.size(); g++) fit += genome[g].fit;
	return fit/genome.size();
}


// :)
//////////////////////////////////////////////////////////////////////////
// Mir

Mir::Mir(int argc, char **argv)
{
	bSoulLog = true;
	id = ++maxId;
	// files
	paramFile = (char *)"params.txt";
	constFile = (char *)"consts";
	popLogFile = (char *)"populationLog.txt";

	cout << "argc = " << argc << endl;
	// seed
	if(argc >= 2) randomSeed = atoi(argv[1]);
	else randomSeed = 1; //time(NULL);
	srand (randomSeed);
	// params, logs
	if(argc >= 3) paramFile = argv[2];
	if(argc >= 4) constFile = argv[3];
	if(argc >= 5) popLogFile = argv[4];
	cout << "params file: " << paramFile << " const file: " << constFile << " pop out file: " << popLogFile << endl;

	printf("new mir: %d\n", id);
	//_consts -- mb remove this (file only)?
	age = 0;
	w = 100;
	h =100;
	NSubstances = 2;
	NSubstanceSources = 10;
	minSubstance_dE = -5;
	maxSubstance_dE = 5;
	orgStartCount = 1000;
	initialOrgGenes = 1;
	initialOrgEnergy = 70;
	energyDivide = initialOrgEnergy*10;
	initialGeneLength = 20;
	diffusion = 0.5;
	substanceDegrade = 0.99;
	expressionCost = 1;
	initialSNPrate = 0.1;
	SourceLifetime = 1000;
	Org::maxAge = 3000; // -1 - no max age
	MirLifetime = 100000000;
	sourceMaxIntensity = 100;
	bSaveGenomes = false;
	minAgeToDivide = 100;

	// not in config yet!
	minSourceRadius = 1;
	maxSourceRadius = 40;
	bSaveGenomes = true; // EXPERIMENTAL

	// LOAD params
	loadConfig();
	// resize
	dE.resize(NSubstances,NSubstances);
	goldSeqs.resize(NSubstances,NSubstances);
	substances.resize(w,h, NSubstances);
	orgs.resize(w,h);
	deadGenomes.resize(w,h);
	sources.reserve(NSubstanceSources);
	// populate
	adam = NULL;
	printf("Mir constructed\n");
}

void Mir::init()
{
	populate_dE();
	populateSources();
	populateGoldSeqs();
	nullPole();
	populateOrgs();
	openLogFiles();
	// geneDistLog -- ExPerImeNTal
	refSeqs.resize(2);
	refSeqs(0) = goldSeqs(0,1); 
	refSeqs(1) = goldSeqs(1,0);
}

void Mir::loadConfig()
{
	FILE *sf;
	sf = fopen(paramFile, "r");
	if(!sf)
	{
		cout << "no config file - working on defaults.\n";
		return;
	}
	char buf[1024];
	float f;
	while(fscanf(sf, "%s %f", buf, &f) == 2)
	{
		if(strcmp(buf, "width") == 0)
		{
			w = (int)f;
			h = w;
		}
		//if(strcmp(buf, "height") == 0) h = (int)f;
		if(strcmp(buf, "substances") == 0) NSubstances = (int)f;
		if(strcmp(buf, "sources") == 0) NSubstanceSources = (int)f;
		if(strcmp(buf, "genes") == 0) initialOrgGenes = (int)f;
		if(strcmp(buf, "geneLength") == 0) initialGeneLength = (int)f;
		if(strcmp(buf, "orgs") == 0) orgStartCount = (int)f;
		if(strcmp(buf, "min_dE") == 0) minSubstance_dE = f;
		if(strcmp(buf, "max_dE") == 0) maxSubstance_dE = f;
		if(strcmp(buf, "startEnergy") == 0) initialOrgEnergy = f;
		if(strcmp(buf, "energyToDivide") == 0) energyDivide = f;
		if(strcmp(buf, "minAgeToDivide") == 0) minAgeToDivide = (int)f;
		if(strcmp(buf, "diffusion") == 0) diffusion = f;
		if(strcmp(buf, "substanceDegrade") == 0) substanceDegrade = f;

		if(strcmp(buf, "expressionCost") == 0) expressionCost = f;
		if(strcmp(buf, "SNPrate") == 0) initialSNPrate = f;
		if(strcmp(buf, "sourceRadius") == 0)
		{
			minSourceRadius = (int)f;
			maxSourceRadius = minSourceRadius;
		}
		if(strcmp(buf, "sourceMaxIntensity") == 0) sourceMaxIntensity = f;


		if(strcmp(buf, "maxAge") == 0) Org::maxAge = (int)f;
		if(strcmp(buf, "SourceLifetime") == 0) SourceLifetime = (int)f;
	}
	fclose(sf);
	sf = fopen(constFile, "r");
	if(!sf)
	{
		cout << "no consts file - working on defaults.\n";
		return;
	}
	while(fscanf(sf, "%s %f", buf, &f) == 2)
	{
		if(strcmp(buf, "MirLifetime") == 0) MirLifetime = (int)f;
		if(strcmp(buf, "substances") == 0) NSubstances = (int)f;
		if(strcmp(buf, "LogFreq") == 0) LOG_FREQ = (int)f;
		if(strcmp(buf, "genes") == 0) initialOrgGenes = (int)f;
		if(strcmp(buf, "saveGenomes") == 0) bSaveGenomes = (bool)f;
		if(strcmp(buf, "PhyloLog") == 0) bSoulLog = (bool)f;
	}
	fclose(sf);
}


Mir::~Mir()
{
	deinit();
}

void Mir::deinit()
{
	for(int i = 0; i < orgsVector.size(); i++)
		delete orgsVector[i];
	orgsVector.clear();
	sources.clear();
	nullPole();
	closeLogFiles();
	age = 0;
	delete adam;
}

void Mir::tic()
{
	sourcesEmmit();
	sourceReincarnate();
	diffuse();
	orgEat();
	orgDie();
	orgDivide();
	if(orgsVector.size() == 0)
	{
		id++;
		populateOrgs();
	}
	logPopulation();
	logGeneDist();
	age++;
}

void Mir::main()
{
	int echoTimer = 0;
	while(age <= MirLifetime)
	{
		tic();
		if(++echoTimer  > 1000)
		{
			printf("mean fit: %f\torgs: %d\tmirAge:%d\n", meanEnzymeFit(), (int)orgsVector.size(), age);
			echoTimer = 0;
		}
	}
	if(bSoulLog)
	{
		giveNames(adam->soul);
		saveGenomes();
		adam->soul->deleteAll();
	}
}

Org* Mir::org(int x, int y)
{
	for(int o = 0; o < orgsVector.size(); o++)
	{
		if(orgsVector[o] == orgs(x,y))
		{
			return orgsVector[o];
		}
	}
	return NULL;
}

//////////////////////////////////////////////////////////////////////////////

void Mir::reportDEMatrix()
{
	printf("*** dE matrix ***\n");
	for(int i = 0; i < NSubstances; i++)
	{
		for(int j = 0; j < NSubstances; j++)
		{
			printf("%f\t", dE(i,j));
		}
		printf("\n");
	}
}

//////////////////////////////////////////////////////////////////////////////
// init

void Mir::nullPole()
{
	// null orgs and substances
	for(int i = 0; i < w; i++)
		for(int j = 0; j < h; j++)
			orgs(i,j) = NULL;
	for(int i = 0; i < w; i++)
		for(int j = 0; j < h; j++)
			for(int s = 0; s < NSubstances; s++)
				substances(i,j,s) = 0;
}

void Mir::populateOrgs()
{
	id = maxId++; // experimental
	//////////////////////////////////////////////////////////////////////////
	orgs.resize(w,h);
	orgsVector.clear();
	orgsVector.reserve(orgStartCount);
	delete(adam);
	adam = new Org(this);
	if(bSoulLog)
	{
		adam->soul->name = "adam";
		adam->soul->alive = true; // not to be killed
	}
	for(int i = 0; i < orgStartCount; i++)
	{
		int x = rand()%w;
		int y = rand()%h;
		if(orgs(x,y) != NULL) continue;
		Org* newbie = new Org(this);
		newbie->x = x;
		newbie->y = y;
		newbie->energy = rand()%(int)initialOrgEnergy;
		// make genome
		newbie->genome.resize(initialOrgGenes);
		for(int g = 0; g < initialOrgGenes; g++)
		{
			newbie->genome[g].seq = randomSeq(initialGeneLength);
			determineEnzyme(newbie->genome[g]);
		}
		newbie->SNPrate = initialSNPrate;
		if(bSoulLog)
		{
			newbie->soul->parent = adam->soul;
			adam->soul->children.push_back(newbie->soul);
		}
		orgs(x,y) = newbie;
		orgsVector.push_back(newbie);
	}
}

void Mir::createRandomSource(SubstanceSource& source)
{
	int goods = goodSubstances.size();
	if(goods <= 0) return;
	source.x = rand()%w;
	source.y = rand()%h;
	int deltaRadius = maxSourceRadius - minSourceRadius;
	if(deltaRadius > 0) source.radius = rand()%deltaRadius + minSourceRadius;
	else source.radius = maxSourceRadius;
	source.substanceId = goodSubstances[ rand()%goods ];
	source.intensity = rand()%sourceMaxIntensity;
	source.age = 0;
}

void Mir::populateSources()
{
	SubstanceSource source;
	if(goodSubstances.size() == 0) return;
	for(int s = 0; s < NSubstanceSources; s++)
	{
		createRandomSource(source);
		sources.push_back(source);
	}
}

void Mir::populate_dE()
{
	for(int i = 0; i < NSubstances; i++)
		for(int j = 0; j <= i; j++)
		{
			if(i == j)
				dE(i,j) = 0;
			else
				dE(i,j) = (rand()%(100*(int)(maxSubstance_dE - minSubstance_dE)))/100.0 + minSubstance_dE;
			dE(j,i) = -dE(i,j);
		}
	reportDEMatrix();
	goodSubstances.clear();
	for(int i =0 ; i < NSubstances; i++)
		if(!badSubstance(i)) goodSubstances.push_back(i);
	cout << "good substances: " << goodSubstances.size() << endl;
}

string Mir::randomSeq(int length)
{
	string tmp;
	tmp.resize(length);
	for(int i = 0; i < length; i++)
	{
		tmp[i] = alphabet[rand()%alphabetLength];
	}
	return tmp;
}

void Mir::populateGoldSeqs()
{
	for(int i = 0; i < NSubstances; i++)
		for(int j = 0; j < NSubstances; j++)
		{
			if(i == j) continue;
			goldSeqs(i,j) = randomSeq(initialGeneLength);
		}
}

float stringDist(string a, string b, bool synOn = true)
{
	int sizeDiff = a.size() - b.size();
	int minSize = min(a.size(), b.size());
	int matched = 0;
	int count = 0;
	for(int i = 0; i < minSize; i++)
	{
		if(synOn && i%3 == 0) continue; // synonymus position
		if(a[i] == b[i]) matched++;
		count++;
	}
	return (1 - (matched/(float)count) + 0.2*sizeDiff);
}

void Mir::determineEnzyme(Gene& gene)
{
	float minDist = 100000000;
	for(int i = 0; i < NSubstances; i++)
		for(int j = 0; j < NSubstances; j++)
		{
			if(i == j) continue;
			float dist = stringDist(gene.seq, goldSeqs(i,j));
			if(dist == 0)
			{
				gene.in = i;
				gene.out = j;
				gene.fit = 1;
				return;
			}
			if(dist < minDist)
			{
				minDist = dist;
				gene.in = i;
				gene.out = j;
			}
		}
	gene.fit = max(1-minDist, 0.0f);
}

//////////////////////////////////////////////////////////////////////////

void Mir::sourcesEmmit()
{
	for(int s = 0; s < sources.size(); s++)
	{
		SubstanceSource source = sources[s];
		// 2bd: add check 4 maximun subst value
		//if(substances(source.x, source.y, source.substanceId) >= maxSubstance)
		//	substances(source.x, source.y, source.substanceId) = maxSubstance;
		int radius = source.radius;
		if(radius == 0) substances(source.x, source.y, source.substanceId) += source.intensity;
		else
		{
			for(int dx = -radius; dx < radius; dx++)
				for(int dy = -radius; dy < radius; dy++)
				{
					int X, Y;
					X = source.x + dx;
					Y = source.y + dy;
					putToWorld(X, Y);
					substances(X, Y, source.substanceId) += source.intensity;
				}
		}
		sources[s].age++;
	}
}

void Mir::sourceReincarnate()
{
	if(SourceLifetime < 0) return;
	for(int s = 0; s < sources.size(); s++)
	{
		if(rand()%SourceLifetime == 1)
		{
			createRandomSource(sources[s]);
		}
	}
}

void Mir::diffuse()
{
	// substanceDegradeDegrade
	lite::array<float[1]> neighbourSum(NSubstances);
	for(int i = 0; i < w; i++)
		for(int j = 0; j < h; j++)
		{
			int count = 0, I, J;
			neighbourSum = 0;
			for(int d_x = -1; d_x <= 1; d_x++)
				for(int d_y = -1; d_y <= 1; d_y++)
				{
					if(d_x == 0 && d_y == 0) continue;
					I = i + d_x;
					J = j + d_y;
					putToWorld(I, J);
					neighbourSum = neighbourSum + substances[row(I)][row(J)];
					count++;
				}
			neighbourSum /= count;
			substances[row(i)][row(j)] = (1 - diffusion)*substances[row(i)][row(j)] + neighbourSum*diffusion;
			substances[row(i)][row(j)] *= substanceDegrade;
		}
}

void Mir::orgEat()
{
	for(int o = 0; o < orgsVector.size(); o++)
	{
		Org* org = orgsVector[o];
		for(int g = 0; g < org->genome.size(); g++)
		{
			Gene* G = &(org->genome[g]);
			float eated = min(substances(org->x, org->y, G->in), Org::maxEAT);
			org->energy += G->fit*eated*dE(G->in, G->out);
			substances(org->x, org->y, G->in) -= eated;
			substances(org->x, org->y, G->out) += eated*G->fit;

			// gene expression cost
			org->energy -= expressionCost;
		}
		org->age++;
		if(org->energy >= Org::maxEnergy) org->energy = Org::maxEnergy;
	}
}

void Mir::orgDie()
{
	int o = orgsVector.size() - 1;
	while(o >= 0 && o != orgsVector.size())
	{
		if(orgsVector[o]->energy <= 0 || orgsVector[o]->age > Org::maxAge)
		{
			Org* org = orgsVector[o];
			orgs(org->x, org->y) = NULL;
			deadGenomes(org->x, org->y) = org->genome;
			orgsVector.erase(orgsVector.begin() + o);
			if(bSoulLog) org->soul->die();
			delete org;
		}
		else o--;
	}
}


void Mir::orgDivide()
{
	int count = orgsVector.size();
	if(count < 1) return;
	vector< lite::array<int[2]> > places; // free nearby cells

	for(int o = 0; o < orgsVector.size(); o++)
	{
		Org* org = orgsVector[o];
		if(org->energy > energyDivide && org->age > minAgeToDivide)
		{
			// place
			places.clear();
			places.reserve(8);
			int x = org->x, y = org->y, X, Y;
			for(int d_x = -1; d_x <= 1; d_x++)
			{
				for(int d_y = -1; d_y <= 1; d_y++)
				{
					if(d_x == 0 && d_y == 0) continue;
					X = x + d_x;
					Y = y + d_y;
					putToWorld(X, Y);
					lite::array<int[2]> tmp(X,Y);
					if(orgs(X,Y) == NULL)
						places.push_back(tmp);
				}
			}
			if(places.size() == 0) continue;
			lite::array<int[2]> place = places[rand()%places.size()];
			//if(orgs(place[0],place[1]) != NULL) continue;
			Org* newbie = org->divide();
			newbie->x = place[0];
			newbie->y = place[1];
			// enzymes
			for(int g = 0; g < newbie->genome.size(); g++)
			{
				determineEnzyme(newbie->genome[g]);
			}
			orgs(newbie->x,newbie->y) = newbie;
			orgsVector.push_back(newbie);
			//if(divideLogOn) fprintf(divideLog, "%d -> %d;\n%d -> %d;\n", org->id, newbie->id, org->id, orgNewId);
		}
	}
}


void Mir::putToWorld(int &x, int &y)
{
	if(x >= w) x -= w;
	if(y >= h) y -= h;

	if(y < 0) y += h;
	if(x < 0) x += w;
//	if(x >= w) x = w-1;
//	if(y >= h) y = h-1;
//	if(x < 0) x = 0;
//	if(y < 0) y = 0;
}

//////////////////////////////////////////////////////////////////////////
// stat helpers

float Mir::meanEnzymeFit()
{
	if(orgsVector.size() == 0) return 0;
	float meanFit = 0;
	int count = 0;
	for(int o = 0; o < orgsVector.size(); o++)
	{
		for(int g = 0; g < orgsVector[o]->genome.size(); g++)
		{
			meanFit +=  orgsVector[o]->genome[g].fit;
			count++;
		}
	}
	return meanFit/count;
}

void Mir::saveGenomes()
{
	char fname[256];
	sprintf(fname, "MirAge_%d.fasta", age);
	FILE* fw = fopen(fname, "w");

	for(int o = 0; o < orgsVector.size(); o++)
	{
		for(int g = 0; g < orgsVector[o]->genome.size(); g++)
		{
			fprintf(fw, ">%s|gene%d\n", orgsVector[o]->soul->name.c_str() , g);
			fprintf(fw, "%s\n", orgsVector[o]->genome[g].seq.c_str());
		}
	}
	fclose(fw);
}



void Mir::giveNames(Soul* soul)
{
	for(int i = 0; i < soul->children.size(); i++)
	{
		std::stringstream ss;
		ss << soul->name  << "_" << i;
		soul->children[i]->name = ss.str();
		giveNames(soul->children[i]);
	}
}

void Mir::calcTaylorMeanVariance(float& mean, float& var)
{
	int dx = w/10;
	int dy = h/10;
	int x0, y0;
	const int maxiter = 50;
	vector<int> n(maxiter);
	for(int i = 0; i < maxiter; i++)
	{
		int count = 0;
		x0  = rand()%(w - dx);
		y0 = rand()%(h - dy);
		for(int x = x0; x < x0+dx; x++)
			for(int y = y0; y < y0+dy; y++)
				if(orgs(x,y) != NULL) count++;
		n[i]=count;
	}
	float sum = std::accumulate(std::begin(n), std::end(n), 0.0);
	float m =  sum / n.size();
	mean = m;
	float accum = 0.0;
	std::for_each (std::begin(n), std::end(n), [&](const float d)
	{
		accum += (d - m) * (d - m);
	});
	var = sqrt(accum / (n.size()-1));
}

/////////////////////////////////////////////////////////////////////


bool Mir::badSubstance(int id)
{
	// energetically non profit to eat
	for(int i = 0; i < NSubstances; i++)
		if(dE(id, i) > 0) return false;
	return true;
}

void Mir::openLogFiles()
{
	populationLog = fopen(popLogFile, "w");
	geneDistLog = fopen("geneDist.txt","w");
	if(!populationLog)
	{
		cout << "problem creating log file... :(\n";
		return;
	}
	fprintf(populationLog, "id\torgs\tmeanFit\tmedianFit\tmaxFit\n");
}

void Mir::closeLogFiles()
{
	fclose(populationLog);
	fclose(geneDistLog);
}

void Mir::logPopulation()
{
	if((age % LOG_FREQ) != 0 || orgsVector.size() == 0) return;
	float meanFit = 0;
	vector<float> fits;
	fits.resize(orgsVector.size());
	for(int o = 0; o < orgsVector.size(); o++)
	{
		meanFit += orgsVector[o]->meanFit();
		fits[o] = orgsVector[o]->meanFit();
	}
	sort(fits.begin(), fits.end());
	float medianFit = fits[orgsVector.size()/2];

	meanFit /= orgsVector.size();
	fprintf(populationLog, "%d\t%d\t%f\t%f\t%f\n", id, (int)orgsVector.size(), meanFit, medianFit, fits[orgsVector.size()-1]);
	fflush(populationLog);
	if (bSaveGenomes) saveGenomes();
}



void Mir::logGeneDist()
{
	// experimental for 1 gene, 2 substances only!
	if((age % LOG_GENEDIST_FREQ) != 0 || orgsVector.size() == 0) return;
	for(int o = 0; o < orgsVector.size(); o++)
        {
                Org* org = orgsVector[o];
		float d1 = stringDist(refSeqs[0], org->genome[0].seq);
		float d2 = stringDist(refSeqs[1], org->genome[0].seq);
		fprintf(geneDistLog, "%d\t%f\t%f\n", age, d1, d2);
	}
}




