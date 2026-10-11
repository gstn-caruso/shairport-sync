#pragma once

class PrincipalParticipant {
public:
  virtual ~PrincipalParticipant() = default;
  virtual int id() const = 0;
  virtual bool mayAcquirePrincipal() const = 0;
  virtual void beginRetirement() = 0;
};
