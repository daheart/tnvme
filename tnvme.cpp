#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include "tnvme.h"
#include "tnvmeHelpers.h"
#include "version.h"
#include "globals.h"


// ------------------------------EDIT HERE---------------------------------
#include "GrpInformative/grpInformative.h"
#include "GrpPciRegisters/grpPciRegisters.h"
#include "GrpCtrlRegisters/grpCtrlRegisters.h"
#include "GrpBasicInit/grpBasicInit.h"


#define NO_DEVICES     "no devices found"


void Usage(void);
void DestroyTestInfrastructure(vector<Group *> &groups, int &fd);
bool BuildTestInfrastructure(vector<Group *> &groups, int &fd,
    struct CmdLine &cl);


void
Usage(void) {
    //80->  xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
    printf("%s (revision %d.%d)\n", APPNAME, VER_MAJOR, VER_MINOR);
    printf("  -h(--help)                          Display this help\n");
    printf("  -v(--rev) <spec>                    All options forced to target specified\n");
    printf("                                      NVME revision {1.0b}; dflt=1.0b\n");
    printf("  -s(--summary)                       Summarize all groups and tests\n");
    printf("  -e(--detail) [<grp> | <grp>:<test>] Detailed group and test description for:\n");
    printf("                                      {all | spec'd_group | test_within_group}\n");
    printf("  -t(--test) [<grp> | <grp>:<test>]   Execute tests for:\n");
    printf("                                      {all | spec'd_group | test_within_group}\n");
    printf("  -f(--informative)                   Run Group Informative tests in addition\n");
    printf("                                      to any test specified by -t(--test)\n");
    printf("  -l(--list)                          List all devices available for test\n");
    printf("  -d(--device) <name>                 Device to open for testing: /dev/node\n");
    printf("                                      dflt=(1st device listed in --list)\n");
    printf("  -r(--rmmap) <space:off:size:acc>    Read memmap'd I/O registers from\n");
    printf("                                      <space>={PCI | BAR01} at <off> bytes\n");
    printf("                                      from start of space for <size> bytes\n");
    printf("                                      access width <acc>={l | w | b} type\n");
    printf("                                      <off:size:acc> require base 16 values\n");
    printf("  -w(--wmmap) <space:off:siz:val:acc> Write <val> data to memmap'd I/O reg from\n");
    printf("                                      <space>={PCI | BAR01} at <off> bytes\n");
    printf("                                      from start of space for <siz> bytes\n");
    printf("                                      access width <acc>={l | w | b} type\n");
    printf("                                      (Require: <size> < 8)\n");
    printf("                                      <offset:size> requires base 16 values\n");
    printf("  -z(--reset)                         Ctrl'r level reset via CC.EN\n");
    printf("  -i(--ignore)                        Ignore detected errors\n");
    printf("  -p(--loop) <count>                  Loop test execution <count> times; dflt=1\n");
    printf("  -k(--skiptest) <filename>           A file contains a list of tests to skip\n");
}


int
main(int argc, char *argv[])
{
    int c;
    int fd = -1;
    int idx = 0;
    int exitCode = 0;
    char *endptr;
    long tmp;
    string work;
    vector<Group *> groups;
    vector<string> devices;
    struct CmdLine CmdLine;
    struct dirent *dirEntry;
    bool deviceFound = false;
    bool accessingHdw = true;
    uint64_t regVal;
    const char *short_opt = "hsflziv:e::p:t::d:k:r:w:";
    static struct option long_opt[] = {
        // {name,           has_arg,            flag,   val}
        {   "help",         no_argument,        NULL,   'h'},
        {   "summary",      no_argument,        NULL,   's'},
        {   "rev",          required_argument,  NULL,   'v'},
        {   "detail",       optional_argument,  NULL,   'e'},
        {   "test",         optional_argument,  NULL,   't'},
        {   "informative",  no_argument,        NULL,   'f'},
        {   "device",       required_argument,  NULL,   'd'},
        {   "rmmap" ,       required_argument,  NULL,   'r'},
        {   "wmmap" ,       required_argument,  NULL,   'w'},
        {   "reset",        no_argument,        NULL,   'z'},
        {   "ignore",       no_argument,        NULL,   'i'},
        {   "loop",         required_argument,  NULL,   'p'},
        {   "skiptest",     required_argument,  NULL,   'k'},
        {   "list",         no_argument,        NULL,   'l'},
        {   NULL,           no_argument,        NULL,    0}
    };

    // defaults if not spec'd on cmd line
    CmdLine.rev = SPECREV_10b;
    CmdLine.detail.req = false;
    CmdLine.test.req = false;
    CmdLine.reset = false;
    CmdLine.summary = false;
    CmdLine.ignore = false;
    CmdLine.informative.req = false;
    CmdLine.loop = 1;
    CmdLine.device = NO_DEVICES;
    CmdLine.rmmap.req = false;
    CmdLine.wmmap.req = false;

    if (argc == 1) {
        printf("%s is a compliance test suite for NVM Express hardware.\n",
            APPNAME);
        exit(0);
    }


    // Seek for all possible devices that this app may commune
    DIR *devDir = opendir("/dev");
    if (devDir == NULL) {
        printf("Unable to open system /dev directory\n");
        exit(1);
    }
    while ((dirEntry = readdir(devDir)) != NULL) {
        work = dirEntry->d_name;
        if (work.find("qnvme") != string::npos)
            devices.push_back("/dev/" + work);
    }
    if (devices.size())
        CmdLine.device = devices[0];    // Default to 1st element listed


    // Parse cmd line options
    while ((c = getopt_long(argc, argv, short_opt, long_opt, &idx)) != -1) {
        switch (c) {

        case 'v':
            if (strcmp("1.0a", optarg) == 0) {
                CmdLine.rev = SPECREV_10b;
            }
            break;

        case 'e':
            if (ParseTargetCmdLine(CmdLine.detail, optarg) == false) {
                printf("Unable to parse --detail cmd line\n");
                exit(1);
            }
            accessingHdw = false;
            break;

        case 't':
            if (ParseTargetCmdLine(CmdLine.test, optarg) == false) {
                printf("Unable to parse --test cmd line\n");
                exit(1);
            }
            break;

        case 'k':
            if (ParseSkipTestCmdLine(CmdLine.skiptest, optarg) == false) {
                printf("Unable to parse --skiptest cmd line\n");
                exit(1);
            }
            break;

        case 'r':
            if (ParseRmmapCmdLine(CmdLine.rmmap, optarg) == false) {
                printf("Unable to parse --rmmap cmd line\n");
                exit(1);
            }
            break;

        case 'w':
            if (ParseWmmapCmdLine(CmdLine.wmmap, optarg) == false) {
                printf("Unable to parse --wmmap cmd line\n");
                exit(1);
            }
            break;

        case 'd':
            work = optarg;
            for (size_t i = 0; i < devices.size(); i++) {
                if (work.compare(devices[i]) == 0) {
                    CmdLine.device = work;
                    deviceFound = true;
                    break;
                }
            }
            if (deviceFound == false) {
                printf("/dev/%s is not among possible devices "
                    "which can be tested\n", work.c_str());
                exit(1);
            }
            break;

        case 'p':
            tmp = strtol(optarg, &endptr, 10);
            if (*endptr != '\0') {
                printf("Unrecognized --loop <count>=%s\n", optarg);
                exit(1);
            } else if (tmp <= 0) {
                printf("Negative/zero values for --loop are unproductive\n");
                exit(1);
            }
            CmdLine.loop = tmp;
            break;

        case 'l':
            printf("Devices available for test:\n");
            if (devices.size() == 0) {
                printf("none\n");
            } else {
                for (size_t i = 0; i < devices.size(); i++) {
                    printf("%s\n", devices[i].c_str());
                }
            }
            exit(0);

        case 's':
            CmdLine.summary = true;
            accessingHdw = false;
            break;

        default:
        case 'h':   Usage();                            exit(0);
        case '?':   Usage();                            exit(1);
        case 'f':   CmdLine.informative.req = true;     break;
        case 'z':   CmdLine.reset = true;               break;
        case 'i':   CmdLine.ignore = true;              break;
        }
    }

    if (optind < argc) {
        printf("Unable to parse all cmd line arguments: %d of %d: ",
            optind, argc);
        while (optind < argc)
            printf("%s ", argv[optind++]);
        printf("\n");
        Usage();
        exit(1);
    }


    // Execute cmd line options which require the test infrastructure
    if (BuildTestInfrastructure(groups, fd, CmdLine) == false)
        exit(1);

    // Accessing hardware requires specific checks and inquiries before running
    if (accessingHdw) {
        // Create globals/singletons here, which all tests objects will need
        gRegisters = Registers::getInstance(fd, CmdLine.rev);

        LOG_NRM("Checking for unintended device under low powered states");
        if (gRegisters->Read(PCISPC_PMCS, regVal) == false) {
            exit(1);
        } else if (regVal & 0x03) {
            LOG_ERR("PCI power state not fully operational, is this intended?");
        }
    }


    if (CmdLine.summary) {
        for (size_t i = 0; i < groups.size(); i++) {
            FORMAT_GROUP_DESCRIPTION(work, groups[i])
            printf("%s\n", work.c_str());
            printf("%s", groups[i]->GetGroupSummary(false).c_str());
        }

    } else if (CmdLine.detail.req) {
        if (CmdLine.detail.t.group == UINT_MAX) {
            for (size_t i = 0; i < groups.size(); i++) {
                FORMAT_GROUP_DESCRIPTION(work, groups[i])
                printf("%s\n", work.c_str());
                printf("%s", groups[i]->GetGroupSummary(true).c_str());
            }

        } else {    // user spec'd a group they are interested in
            if (CmdLine.detail.t.group >= groups.size()) {
                printf("Specified test group does not exist\n");

            } else {
                for (size_t i = 0; i < groups.size(); i++) {
                    if (i == CmdLine.detail.t.group) {
                        FORMAT_GROUP_DESCRIPTION(work, groups[i])
                        printf("%s\n", work.c_str());

                        if ((CmdLine.detail.t.major == UINT_MAX) ||
                            (CmdLine.detail.t.minor == UINT_MAX)) {
                            // Want info on all tests within group
                            printf("%s",
                                groups[i]->GetGroupSummary(true).c_str());
                        } else {
                            // Want info on spec'd test within group
                            printf("%s", groups[i]->GetTestDescription(true,
                                CmdLine.detail.t).c_str());
                            break;
                        }
                    }
                }
            }
        }
    } else if (CmdLine.rmmap.req) {
        uint8_t *value = new uint8_t[CmdLine.rmmap.size];
        gRegisters->Read(CmdLine.rmmap.space, CmdLine.rmmap.size,
            CmdLine.rmmap.offset, CmdLine.rmmap.acc, value);
        string result = gRegisters->FormatRegister(CmdLine.rmmap.space,
            CmdLine.rmmap.size, CmdLine.rmmap.offset, value);
        printf("%s\n", result.c_str());
    } else if (CmdLine.wmmap.req) {
        gRegisters->Write(CmdLine.wmmap.space, CmdLine.wmmap.size,
            CmdLine.wmmap.offset, CmdLine.wmmap.acc, (uint8_t *)(&CmdLine.wmmap.value));
    } else if (CmdLine.reset) {
        ;   // todo; add some reset logic when available
    } else if (CmdLine.test.req) {
        exitCode = ExecuteTests(CmdLine, groups) ? 0 : 1;
    }

    DestroyTestInfrastructure(groups, fd);
    exit(exitCode);
}


/**
 * A function to perform the necessary duties to create the objects, interact
 * with the OS which will allow testing to commence according to the cmd line
 * options presented to this app.
 * @param groups Pass the structure to contain the test objects
 * @param fd Pass the file descriptor to associate with the device
 * @param cl Pass the cmd line args
 * @return true upon success, otherwise false
 */
bool
BuildTestInfrastructure(vector<Group *> &groups, int &fd,
    struct CmdLine &cl)
{
    struct flock fdlock = {F_WRLCK, SEEK_SET, 0, 0, 0};


    DestroyTestInfrastructure(groups, fd);

    // Open and lock access to the device requested for testing. The mutually
    // exclusive write lock is expected to warrant off other instances of this
    // app from choosing to test against the same device. An app which has
    // attained a lock on the target device may have multiple threads which
    // could cause testing corruption, and therefore a single threaded device
    // interaction model is needed. No more than 1 test can occur at any time
    // to any device and all tests must be single threaded.
    if (cl.device.compare(NO_DEVICES) == 0) {
        printf("There are no devices present\n");
        return false;
    }
    if ((fd = open(cl.device.c_str(), O_RDWR)) == -1) {
        if ((errno == EACCES) || (errno == EAGAIN)) {
            printf("%s may need permission set for current user\n",
                cl.device.c_str());
        }
        LOG_ERR("device=%s: %s", cl.device.c_str(), strerror(errno));
        return false;
    }
    if (fcntl(fd, F_SETLK, &fdlock) == -1) {
        if ((errno == EACCES) || (errno == EAGAIN)) {
            printf("%s has been locked by another process\n",
                cl.device.c_str());
        }
        LOG_ERR("%s", strerror(errno));
    }


    // ------------------------------EDIT HERE---------------------------------
    // IMPORTANT: Once a group is assigned/push_back() to a position in the
    //            vector, i.e. a group index/number, then it should stay in that
    //            position so that the group references won't change per
    //            release. Future groups can be appended 1, 2, 3, 4, etc., but
    //            existing groups need to keep their original assigned vector
    //            position so users of this app can rely on these assignments.
    //
    // All groups will be pushed here. The groups themselves dictate which
    // tests get executed based upon the constructed 'specRev' being targeted.
    cl.informative.grpInfoIdx = groups.size();  // tied to the next statement
    groups.push_back(new GrpInformative(groups.size(), cl.rev, fd));       // 0
    groups.push_back(new GrpPciRegisters(groups.size(), cl.rev, fd));      // 1
    groups.push_back(new GrpCtrlRegisters(groups.size(), cl.rev, fd));     // 2
    groups.push_back(new GrpBasicInit(groups.size(), cl.rev, fd));         // 3

    return true;
}


/**
 * Tear down that which has been created by BuildTestInfrastructure()
 * @param groups Pass the structure to contain the test objects, if the
 *               structure is not empty the function will abort.
 * @param fd Pass the file descriptor to free from the allocated resource pool
 */
void
DestroyTestInfrastructure(vector<Group *> &groups, int &fd)
{
    // Deallocate heap usage
    for (size_t i = 0; i < groups.size(); i++) {
        delete groups.back();
        groups.pop_back();
    }

    // If it fails what do we do? ignore it for now
    if (fd != -1) {
        if (close(fd) == -1)
            LOG_ERR("%s", strerror(errno));
    }
}
