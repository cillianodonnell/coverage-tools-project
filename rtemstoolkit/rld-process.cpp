/*
 * Copyright (c) 2011, Chris Johns <chrisj@rtems.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#ifndef WIFEXITED
#define WIFEXITED(S) (((S) & 0xff) == 0)
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(S) (((S) & 0xff00) >> 8)
#endif
#ifndef WIFSIGNALED
#define WIFSIGNALED(S) (((S) & 0xff) != 0 && ((S) & 0xff) != 0x7f)
#endif
#ifndef WTERMSIG
#define WTERMSIG(S) ((S) & 0x7f)
#endif
#ifndef WIFSTOPPED
#define WIFSTOPPED WIFEXITED
#endif
#ifndef WSTOPSIG
#define WSTOPSIG WEXITSTATUS
#endif

#if __WIN32__
#define CREATE_MODE (S_IRUSR | S_IWUSR)
#define OPEN_FLAGS  (O_BINARY)
#else
#define CREATE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH)
#define OPEN_FLAGS  (0)
#endif

#include <iostream>

#include "rld.h"
#include "rld-process.h"

#include <libiberty.h>

namespace rld
{
  namespace process
  {
    /**
     * Global keep of temporary files if true. Used to help debug a system.
     */
    bool keep_temporary_files = false;

    /**
     * The temporary files.
     */
    temporary_files temporaries;

    temporary_files::temporary_files ()
    {
    }

    temporary_files::~temporary_files ()
    {
      clean_up ();
    }

    const std::string
    temporary_files::get (const std::string& suffix, bool keep)
    {
      char* temp = ::make_temp_file (suffix.c_str ());

      if (!temp)
        throw rld::error ("bad temp name", "temp-file");

      const std::string name = rld::find_replace (temp,
                                                  RLD_PATH_SEPARATOR_STR RLD_PATH_SEPARATOR_STR,
                                                  RLD_PATH_SEPARATOR_STR);
      tempfile_ref ref (name, keep);
      tempfiles.push_back (ref);
      return name;
    }

    void
    temporary_files::unlink (const tempfile_ref& ref)
    {
      if (!keep_temporary_files && !ref.keep)
        rld::path::unlink (ref.name);
    }

    void
    temporary_files::erase (const std::string& name)
    {
      for (tempfile_container::iterator tfi = tempfiles.begin ();
           tfi != tempfiles.end ();
           ++tfi)
      {
        if ((*tfi).name == name)
        {
          unlink (*tfi);
          tempfiles.erase (tfi);
          break;
        }
      }
    }

    void
    temporary_files::keep (const std::string& name)
    {
      for (tempfile_container::iterator tfi = tempfiles.begin ();
           tfi != tempfiles.end ();
           ++tfi)
      {
        if ((*tfi).name == name)
        {
          (*tfi).keep = true;
          break;
        }
      }
    }

    void
    temporary_files::clean_up ()
    {
      for (tempfile_container::iterator tfi = tempfiles.begin ();
           tfi != tempfiles.end ();
           ++tfi)
      {
        unlink (*tfi);
      }
    }

    tempfile::tempfile (const std::string& suffix, bool _keep)
      : suffix (suffix),
        overridden (false),
        fd (-1),
        level (0)
    {
      _name = temporaries.get (suffix, _keep);
    }

    tempfile::~tempfile ()
    {
      close ();
      temporaries.erase (_name);
    }

    void
    tempfile::open (bool writable)
    {
      if (fd < 0)
      {
        bool ok = rld::path::check_file (_name);
        int  flags = writable ? O_RDWR : O_RDONLY;
        int  mode = writable ? CREATE_MODE : 0;

        if (writable && overridden)
        {
          flags |= O_CREAT | O_TRUNC | O_APPEND;
        }
        else
        {
          if (!ok)
            throw rld::error ("Not found.", "tempfile open:" + _name);
        }

        level = 0;
        fd = ::open (_name.c_str (), flags, mode);
        if (fd < 0)
          throw rld::error (::strerror (errno), "tempfile open:" + _name);
      }
    }

    void
    tempfile::close ()
    {
      if (fd != -1)
      {
        ::close (fd);
        fd = -1;
        level = 0;
      }
    }

    void
    tempfile::override (const std::string& name_)
    {
      if (fd >= 0)
        throw rld::error ("Already open", "tempfile override");
      rld::path::unlink (_name);
      overridden = true;
      _name = name_ + suffix;
    }

    void
    tempfile::keep ()
    {
      temporaries.keep (_name);
    }

    const std::string&
    tempfile::name () const
    {
      return _name;
    }

    size_t
    tempfile::size ()
    {
      if (fd < 0)
        return 0;

      struct stat sb;
      if (::stat (_name.c_str (), &sb) == 0)
        return sb.st_size;

      return 0;
    }

    void
    tempfile::read (std::string& all)
    {
      all.clear ();
      if (fd != -1)
      {
        if (level)
          all.append (buf, level);
        level = 0;
        while (true)
        {
          int read = ::read (fd, buf, sizeof (buf) );
          if (read < 0)
            throw rld::error (::strerror (errno), "tempfile get read:" + _name);
          else if (read == 0)
            break;
          else
            all.append (buf, read);
        }
      }
    }

    void
    tempfile::read_line (std::string& line)
    {
      line.clear ();
      if (fd != -1)
      {
        bool reading = true;
        while (reading)
        {
          if (level < (sizeof (buf) - 1))
          {
            ::memset (buf + level, 0, sizeof (buf) - level);
            int read = ::read (fd, buf + level, sizeof (buf) - level - 1);
            if (read < 0)
              throw rld::error (::strerror (errno), "tempfile read:" + _name);
            else if (read == 0)
              reading = false;
            else
              level += read;
          }
          if (level)
          {
            char* lf = ::strchr (buf, '\n');
            int   len = level;
            if (lf)
            {
              len = lf - &buf[0] + 1;
              reading = false;
            }
            line.append (buf, len);
            level -= len;
            if (level)
              ::memmove (buf, &buf[len], level + 1);
          }
        }
      }
    }

    void
    tempfile::write (const std::string& s)
    {
      const char* p = s.c_str ();
      size_t      l = s.length ();
      while (l)
      {
        int written = ::write (fd, p, l);
        if (written < 0)
            throw rld::error (::strerror (errno), "tempfile write:" + _name);
        if (written == 0)
          break;
        l -= written;
      }
    }

    void
    tempfile::write_line (const std::string& s)
    {
      write (s);
      write (RLD_LINE_SEPARATOR);
    }

    void
    tempfile::write_lines (const rld::strings& ss)
    {
      for (rld::strings::const_iterator ssi = ss.begin ();
           ssi != ss.end ();
           ++ssi)
      {
        write_line (*ssi);
      }
    }

    void
    tempfile::output (std::ostream& out)
    {
      std::string prefix;
      output (prefix, out);
    }

    void
    tempfile::output (const std::string& prefix,
                      std::ostream&      out,
                      bool               line_numbers)
    {
      if (fd == -1)
      {
        std::string line;
        int         lc = 0;
        open ();
        while (true)
        {
          read_line (line);
          ++lc;
          if (line.empty () && (level == 0))
            break;
          if (!prefix.empty ())
            out << prefix << ": ";
          if (line_numbers)
            out << lc << ": ";
          out << line << std::flush;
        }
        close ();
      }
    }

    void
    set_keep_temporary_files ()
    {
      keep_temporary_files = true;
    }

    void
    temporaries_clean_up ()
    {
      temporaries.clean_up ();
    }

    void
    args_append (arg_container& args, const std::string& str)
    {
      rld::strings ss;
      rld::split (ss, str);
      for (rld::strings::iterator ssi = ss.begin ();
           ssi != ss.end ();
           ++ssi)
      {
        args.push_back (*ssi);
      }
    }

    status
    execute (const std::string& pname,
             const std::string& command,
             const std::string& outname,
             const std::string& errname)
    {
      arg_container args;
      parse_command_line (command, args);
      return execute (pname, args, outname, errname);
    }

    status
    execute (const std::string&   pname,
             const arg_container& args,
             const std::string&   outname,
             const std::string&   errname)
    {
      if (rld::verbose (RLD_VERBOSE_TRACE))
      {
        std::cout << "execute: ";
        for (size_t a = 0; a < args.size (); ++a)
          std::cout << args[a] << ' ';
        std::cout << std::endl;
      }

      const char** cargs = new const char* [args.size () + 1];

      for (size_t a = 0; a < args.size (); ++a)
        cargs[a] = args[a].c_str ();
      cargs[args.size ()] = 0;

      int err = 0;
      int s = 0;

      const char* serr = pex_one (PEX_LAST | PEX_SEARCH,
                                  args[0].c_str (),
                                  (char* const*) cargs,
                                  pname.c_str (),
                                  outname.c_str (),
                                  errname.c_str (),
                                  &s,
                                  &err);

      delete [] cargs;

      if (serr)
        throw rld::error ("execute: " + args[0], serr);
      else if (err)
        throw rld::error ("execute: " + args[0], ::strerror (err));

      status _status;

      if (rld::verbose (RLD_VERBOSE_TRACE))
        std::cout << "execute: status: ";

      if (WIFEXITED (s))
      {
        _status.type = status::normal;
        _status.code = WEXITSTATUS (s);
        if (rld::verbose (RLD_VERBOSE_TRACE))
          std::cout << _status.code << std::endl;
      }
      else if (WIFSIGNALED (s))
      {
        _status.type = status::signal;
        _status.code = WTERMSIG (s);
        if (rld::verbose (RLD_VERBOSE_TRACE))
          std::cout << "signal: " << _status.code << std::endl;
      }
      else if (WIFSTOPPED (s))
      {
        _status.type = status::stopped;
        _status.code = WSTOPSIG (s);
        if (rld::verbose (RLD_VERBOSE_TRACE))
          std::cout << "stopped: " << _status.code << std::endl;
      }
      else
        throw rld::error ("execute: " + args[0], "unknown status returned");

      return _status;
    }

    /*
     * The code is based on this C file:
     *  http://cybertiggyr.com/pcm/src/parse.c
     */
    void
    parse_command_line (const std::string& command, arg_container& args)
    {
      enum pstate
      {
        pstate_discard_space,
        pstate_accumulate_quoted,
        pstate_accumulate_raw
      };

      args.clear ();

      const char quote = '"';
      const char escape = '\\';
      pstate     state = pstate_discard_space;
      size_t     start = 0;
      size_t     i = 0;

      while (i < command.size ())
      {
        switch (state)
        {
          case pstate_discard_space:
            if (command[i] == quote)
            {
              ++i;
              start = i;
              state = pstate_accumulate_quoted;
            }
            else if (::isspace (command[i]))
            {
              ++i;
            }
            else /* includes escape */
            {
              start = i;
              state = pstate_accumulate_raw;
            }
            break;

          case pstate_accumulate_quoted:
            if (command[i] == quote)
            {
              args.push_back (command.substr (start, i - 1));
              ++i;
              state = pstate_discard_space;
            }
            else if ((command[i] == escape) && (command[i + 1] == quote))
            {
              i += 2;
            }
            else /* includes space */
            {
              ++i;
            }
            break;

          case pstate_accumulate_raw:
            if (command[i] == quote)
            {
              throw rld::error ("quote in token", "command parse");
            }
            else if ((command[i] == escape) && (command[i + 1] == quote))
            {
              i += 2;
            }
            else if (::isspace (command[i]))
            {
              args.push_back (command.substr (start, i - 1));
              ++i;
              state = pstate_discard_space;
            }
            else
            {
              ++i;
            }
            break;
        }
      }
    }
  }
}
