#pragma once

class PrincipalParticipant {
public:
  virtual ~PrincipalParticipant() = default;
  // Identity stays stable while selected; release selection before destroying the participant.
  virtual int id() const = 0;
  virtual bool mayAcquirePrincipal() const = 0;
  virtual void beginRetirement() = 0;
};
